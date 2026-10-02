#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include <atomic>
#include <array>
#include <memory>
#include <mutex>

struct SharedSample
{
    juce::AudioBuffer<float> audio;
    double sourceSampleRate = 44100.0;
    juce::File sourceFile;
    double peak = 0;
    std::array<double, 2> dc {};
    std::atomic<unsigned> voiceReferences { 0 };
};

class SamplePool
{
public:
    static SamplePool& instance();
    std::shared_ptr<SharedSample> load(const juce::File& file, juce::String& error);
    void pruneExpired();
    std::shared_ptr<SharedSample> aliasFile(const juce::File&, std::shared_ptr<SharedSample>);

private:
    SamplePool();
    juce::String keyFor(const juce::File& file) const;

    juce::AudioFormatManager formats;
    std::mutex mutex;
    std::map<juce::String, std::weak_ptr<SharedSample>> samples;
};
