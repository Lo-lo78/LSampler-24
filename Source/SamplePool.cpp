#include "SamplePool.h"
#include "SamplePoolProgress.h"

namespace {
thread_local const lsampler::SampleLoadProgress* activeSampleLoadProgress = nullptr;
}

namespace lsampler {
std::shared_ptr<SharedSample> loadSampleWithProgress(const juce::File& file, juce::String& error,
                                                     const SampleLoadProgress& progress)
{
    const juce::ScopedValueSetter<const SampleLoadProgress*> current(activeSampleLoadProgress,
                                                                     progress ? &progress : nullptr);
    return SamplePool::instance().load(file, error);
}
}


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
    const auto* progress = activeSampleLoadProgress;
    if (progress) (*progress)(0.0);
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
            {
                if (progress) (*progress)(1.0);
                return existing;
            }
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
    // For WAVs, decoding by ranges produces the same PCM frames and lets us
    // analyse each range while its data is already hot in memory. Other
    // formats retain the original single-read path to avoid codec differences.
    const bool progressiveWav = progress != nullptr && (file.hasFileExtension(".wav") || file.hasFileExtension(".wave"));
    if (progressiveWav)
    {
        // Decode and analyse in the same pass. The order of accumulation for
        // each channel is unchanged; no audio data or parameter is modified.
        constexpr int chunkFrames = 262144;
        const int length = sample->audio.getNumSamples();
        std::array<double, 2> sums {};
        for (int offset = 0; offset < length;)
        {
            const int frames = juce::jmin(chunkFrames, length - offset);
            reader->read(&sample->audio, offset, frames, offset, true, true);
            for (int ch = 0; ch < channels; ++ch)
            {
                const auto* data = sample->audio.getReadPointer(ch, offset);
                for (int i = 0; i < frames; ++i)
                {
                    sums[size_t(ch)] += data[i];
                    sample->peak = std::max(sample->peak, double(std::abs(data[i])));
                }
            }
            offset += frames;
            (*progress)(double(offset) / double(length));
        }
        for (int ch = 0; ch < channels; ++ch)
            sample->dc[size_t(ch)] = sums[size_t(ch)] / length;
    }
    else
        reader->read(&sample->audio, 0, sample->audio.getNumSamples(), 0, true, true);

    if (!progressiveWav)
    {
        // Unmodified decoder/analysis path for non-WAVs and regular host state.
        for (int ch = 0; ch < channels; ++ch) {
            double sum = 0;
            const auto* data = sample->audio.getReadPointer(ch);
            for (int i = 0; i < sample->audio.getNumSamples(); ++i) {
                sum += data[i]; sample->peak = std::max(sample->peak, double(std::abs(data[i])));
            }
            sample->dc[size_t(ch)] = sum / sample->audio.getNumSamples();
        }
    }
    if (channels == 1) sample->dc[1] = sample->dc[0];
    if (progress) (*progress)(1.0);
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


bool SamplePool::canReadFile(const juce::File& file)
{
    if (!file.existsAsFile() || formats.findFormatForFileExtension(file.getFileExtension()) == nullptr)
        return false;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    return reader != nullptr;
}

juce::String SamplePool::getSupportedAudioWildcard() const
{
    return formats.getWildcardForAllFormats();
}

// Browser enumeration must not open every audio file merely to list its name.
// Actual readability is checked by load/canReadFile when the file is used.
bool SamplePool::hasSupportedExtension(const juce::File& file) const
{
    return formats.findFormatForFileExtension(file.getFileExtension()) != nullptr;
}
