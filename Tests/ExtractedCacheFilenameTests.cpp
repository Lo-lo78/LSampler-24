#include "../Source/ExtractedCacheFilename.h"
#include <cassert>
#include <iostream>

int main()
{
    using lsampler_extracted_cache::isGeneratedFileName;
    const std::string digest = "123456789abcdef012345678"; // 24 hex characters
    assert(digest.size() == 24);
    assert(isGeneratedFileName("recording_" + digest + "_ch01-02.wav"));
    assert(isGeneratedFileName("sample_wide_multi_" + digest + "_ch15-16.WAV"));
    assert(isGeneratedFileName("video_" + digest + "_ch05-05.wav.partial-7acb01"));
    assert(!isGeneratedFileName("personal.wav"));
    assert(!isGeneratedFileName("recording_" + digest + ".wav"));
    assert(!isGeneratedFileName("recording_" + digest + "_ch01-02.mp4"));
    assert(!isGeneratedFileName("recording_" + digest + "_ch01-02.wav.bak"));
    assert(!isGeneratedFileName("recording_1234_ch01-02.wav"));
    assert(!isGeneratedFileName("recording_" + digest + "_ch01-02.wav.partial-zzzz"));
    assert(!isGeneratedFileName("../recording_" + digest + "_ch01-02.wav/extra"));
    std::cout << "PASS TEST183: only LSampler-generated cache WAV/partial names match\n";
}
