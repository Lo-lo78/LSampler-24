#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include <memory>
#include <mutex>

struct SharedSample
{
    juce::AudioBuffer<float> audio;
    double sourceSampleRate = 44100.0;
    juce::File sourceFile;
};

class SamplePool
{
public:
    static SamplePool& instance();
    std::shared_ptr<SharedSample> load(const juce::File& file, juce::String& error);
    void pruneExpired();

private:
    SamplePool();
    juce::String keyFor(const juce::File& file) const;

    juce::AudioFormatManager formats;
    std::mutex mutex;
    std::map<juce::String, std::weak_ptr<SharedSample>> samples;
};
