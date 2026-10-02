#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

class LibraryManager
{
public:
    explicit LibraryManager(const juce::File& rootOverride = {});

    const juce::File& root() const noexcept { return rootDir; }
    const juce::File& samples() const noexcept { return samplesDir; }
    const juce::File& slots() const noexcept { return slotsDir; }
    const juce::File& banks() const noexcept { return banksDir; }

    juce::File materialiseSample(const juce::File& source, juce::String& error) const;
    juce::File materialiseSampleAtRelativePath(const juce::File& source, const juce::String& relativePath, juce::String& error) const;
    juce::String makeSampleReference(const juce::File& sampleFile) const;
    juce::File resolveSampleReference(const juce::String& reference) const;

    static constexpr const char* slotExtension = ".lsampler-24-s";
    static constexpr const char* bankExtension = ".lsampler-24-b";
    static juce::String defaultName(const juce::String& prefix);

private:
    static juce::File uniqueDestination(const juce::File& folder, const juce::String& fileName);

    juce::File rootDir;
    juce::File samplesDir;
    juce::File slotsDir;
    juce::File banksDir;
};
