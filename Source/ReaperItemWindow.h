#pragma once

// TEST176 - Pure geometry validation for importing an unprocessed REAPER item
// from its original PCM file. This intentionally rejects transformations that
// cannot be represented by a simple source-file subrange. No REAPER/JUCE types
// or DAW calls are required; the calculation can be unit-tested on any OS.
#include <cmath>

namespace lsampler_reaper_item {

struct Window
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
};

inline bool computeUnprocessedWindow(double takeStartOffset, double itemLength,
                                    double sourceLength, double takePlayRate,
                                    double takePitchSemitones, int stretchMarkers,
                                    bool hasWrappedSource, int takeChannelMode,
                                    Window& out) noexcept
{
    out = {};
    if (!std::isfinite(takeStartOffset) || !std::isfinite(itemLength)
        || !std::isfinite(sourceLength) || !std::isfinite(takePlayRate)
        || !std::isfinite(takePitchSemitones)) return false;

    // Length/offset are in source seconds only when the take plays at 1x.
    // REVERSE/SECTION wrappers, stretch, repitch, channel processing, or
    // looping beyond the media end need a separate rendered-audio importer.
    if (hasWrappedSource || stretchMarkers != 0 || takeChannelMode != 0
        || std::abs(takePlayRate - 1.0) > 1e-8
        || std::abs(takePitchSemitones) > 1e-8
        || takeStartOffset < 0.0 || itemLength <= 0.0005
        || sourceLength <= 0.0005 || takeStartOffset >= sourceLength)
        return false;

    const double last = takeStartOffset + itemLength;
    if (!std::isfinite(last) || last > sourceLength + 0.0001) return false;
    out = { takeStartOffset, last > sourceLength ? sourceLength : last };
    return out.endSeconds > out.startSeconds + 0.0005;
}

} // namespace lsampler_reaper_item
