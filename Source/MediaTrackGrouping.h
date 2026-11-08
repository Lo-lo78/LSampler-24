#pragma once
// TEST182: pure-C++ definition of consecutive channel pairs; no downmix,
// no counter based on source FILES rather than expanded stereo tracks.
#include <algorithm>

namespace lsampler_media_grouping
{
inline constexpr int trackCount(int channels) noexcept
{
    return channels > 0 ? (channels / 2 + channels % 2) : 0;
}
struct Pair
{
    int firstChannel = 0; // zero-based
    int channelCount = 0; // 2, or a final mono channel
};
inline constexpr Pair pairAt(int channels, int index) noexcept
{
    return index < 0 || index >= trackCount(channels) ? Pair{}
        : Pair{index * 2, std::min(2, channels - index * 2)};
}
inline constexpr int limitedTracks(int channels, int remainingSlots) noexcept
{
    return std::max(0, std::min(trackCount(channels), remainingSlots));
}
}
