#include "SamplePool.h"

SamplePool& SamplePool::instance()
{
    static SamplePool pool;
    return pool;
}

SamplePool::SamplePool()
{
    formats.registerBasicFormats();
}

juce::String SamplePool::keyFor(const juce::File& file) const
{
    return file.getFullPathName().replaceCharacter('\\', '/').toLowerCase();
}

std::shared_ptr<SharedSample> SamplePool::load(const juce::File& file, juce::String& error)
{
    error.clear();
    if (!file.existsAsFile())
    {
        error = "Sample file not found";
        return {};
    }

    const auto key = keyFor(file);
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (auto it = samples.find(key); it != samples.end())
            if (auto existing = it->second.lock())
                return existing;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "Unsupported or unreadable audio file";
        return {};
    }

    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        error = "Invalid sample length";
        return {};
    }

    auto sample = std::make_shared<SharedSample>();
    sample->sourceFile = file;
    sample->sourceSampleRate = reader->sampleRate;
    const int channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    sample->audio.setSize(channels, static_cast<int>(reader->lengthInSamples));
    reader->read(&sample->audio, 0, sample->audio.getNumSamples(), 0, true, true);

    // One shared analysis pass at decode time, outside all realtime paths.
    for (int ch = 0; ch < channels; ++ch) {
        double sum = 0;
        const auto* data = sample->audio.getReadPointer(ch);
        for (int i = 0; i < sample->audio.getNumSamples(); ++i) {
            sum += data[i]; sample->peak = std::max(sample->peak, double(std::abs(data[i])));
        }
        sample->dc[size_t(ch)] = sum / sample->audio.getNumSamples();
    }
    if (channels == 1) sample->dc[1] = sample->dc[0];
    {
        std::lock_guard<std::mutex> lock(mutex);
        // Concurrent decodes may race; reuse the winner rather than retaining duplicate audio.
        if (auto existing = samples[key].lock()) return existing;
        samples[key] = sample;
    }
    return sample;
}

void SamplePool::pruneExpired()
{
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = samples.begin(); it != samples.end();)
        if (it->second.expired()) it = samples.erase(it); else ++it;
}

std::shared_ptr<SharedSample> SamplePool::aliasFile(const juce::File& file, std::shared_ptr<SharedSample> sample) {
    const auto key = keyFor(file);
    std::lock_guard<std::mutex> lock(mutex);
    if(auto existing = samples[key].lock()) return existing;
    samples[key] = sample;
    return sample;
}
