#pragma once
// TEST182: expand a copied audio/video source into one mono/stereo WAV per
// consecutive pair of source channels. Runs exclusively on the file worker.
// The extracted WAVs are persistent, since VST3 projects refer to file paths.
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>
#include <vector>

namespace lsampler_media_paste
{
struct Track
{
    juce::File file;
    juce::String suffix;  // e.g. " [channels 1-2]"; empty for native mono/stereo
};
using StopRequested = std::function<bool()>;
using Progress = std::function<void(double)>;

// A conservative list of MEDIA extensions in addition to JUCE's native audio
// formats. A supported extension never guarantees a Windows decoder exists.
bool isCandidate(const juce::File& file);

// One input can expand to N stereo/mono tracks. maxTracks limits decoding / disk
// work to the remaining 24-slot budget. Excess is reported via discardedTracks.
std::vector<Track> expand(const juce::File& source, int maxTracks,
                          const StopRequested& shouldStop,
                          const Progress& progress,
                          juce::String& error, int& discardedTracks);
}
