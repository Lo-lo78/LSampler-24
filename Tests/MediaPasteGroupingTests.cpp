#include "../Source/MediaTrackGrouping.h"
#include "../Source/ChromaticPasteMapping.h"
#include <array>
#include <cassert>
#include <iostream>

int main()
{
    using namespace lsampler_media_grouping;
    assert(trackCount(0) == 0);
    assert(trackCount(1) == 1 && trackCount(2) == 1);
    assert(trackCount(3) == 2 && trackCount(5) == 3);
    assert(trackCount(8) == 4 && trackCount(16) == 8);
    assert(pairAt(8, 0).firstChannel == 0 && pairAt(8, 0).channelCount == 2);
    assert(pairAt(8, 3).firstChannel == 6 && pairAt(8, 3).channelCount == 2);
    assert(pairAt(5, 2).firstChannel == 4 && pairAt(5, 2).channelCount == 1);
    assert(limitedTracks(16, 3) == 3);

    std::array<lsampler_chromatic_paste::SlotMapping, 24> initial{};
    lsampler_chromatic_paste::BatchPastePlanner<24> plan(initial, 0);
    // Two consecutive 16-channel files become EIGHT independent stereo
    // tracks each, not two physical source-file slots.
    for (int file = 0; file < 2; ++file)
        for (int pair = 0; pair < trackCount(16); ++pair)
        {
            const auto target = plan.assign(false);
            assert(target);
            assert(target.slot == file * 8 + pair);
            assert(target.note == 60 + file * 8 + pair);
        }
    // At slot 17 there are eight remaining physical slots.
    assert(plan.pendingSlot() == 16);
    for (int pair = 0; pair < trackCount(8); ++pair)
    {
        const auto next = plan.assign(false);
        assert(next && next.slot == 16 + pair && next.note == 76 + pair);
    }

    // TEST181 occupied-slot dialog still works once tracks are expanded:
    initial[2] = {true, 62.0, 62.0, 62.0};
    lsampler_chromatic_paste::BatchPastePlanner<24> relocate(initial, 0);
    assert(relocate.assign(false).slot == 0);
    assert(relocate.assign(false).slot == 1);
    assert(relocate.needsOverwriteDecision());
    const auto third = relocate.assign(false); // No => 4th slot (zero-based 3)
    assert(third && third.slot == 3 && third.note == 63);
    assert(relocate.assign(false).slot == 4);
    std::cout << "PASS TEST182: 1/2/3/8/16-channel grouping, second 16-channel file begins at slot 9, no-overwrite relocation\n";
}
