#include "../Source/ChromaticPasteMapping.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

using lsampler_chromatic_paste::SlotMapping;
using lsampler_chromatic_paste::chromaticNote;
using lsampler_chromatic_paste::firstNote;

static void expect(bool ok, const char* scenario)
{
    if (!ok) { std::cerr << "TEST180 chromatic paste: " << scenario << '\n'; std::exit(1); }
}
static SlotMapping chromatic(int note)
{
    return { true, double(note), double(note), double(note) };
}

int main()
{
    std::array<SlotMapping, 24> slots{};
    expect(firstNote(slots, 0) == 60, "empty bank starts at MIDI 60");
    expect(firstNote(slots, 9) == 60, "empty prefix starts at MIDI 60 even at slot 10");

    // Simulate an import of 4 REAPER items at slots 1..4.
    for (int i = 0; i < 4; ++i) slots[size_t(i)] = chromatic(60 + i);
    expect(firstNote(slots, 4) == 64, "second Alt+V from Explorer continues at 64");
    // Simulate Explorer importing 3 files at slots 5..7.
    for (int i = 4; i < 7; ++i) slots[size_t(i)] = chromatic(60 + i);
    expect(firstNote(slots, 7) == 67, "third Alt+V resumes at 67");
    expect(firstNote(slots, 2) == 62, "overwrite preserves destination's note");
    expect(firstNote(slots, 10) == 70, "gaps retain note/slot offsets");

    slots[6] = {true, 50, 80, 65};
    expect(chromaticNote(slots[6]) == -1, "custom key range is not chromatic");
    expect(firstNote(slots, 7) == 67, "custom predecessor is ignored in favour of earlier anchor");
    slots[0] = {false, 90, 90, 90};
    expect(chromaticNote(slots[0]) == -1, "an empty slot is never a pitch anchor");

    slots.fill({});
    slots[0] = chromatic(127);
    expect(firstNote(slots, 1) == 128, "MIDI exhaustion signalled, not clamped to 127");
    slots[4] = chromatic(125);
    expect(firstNote(slots, 4) == 125, "overwrite at MIDI 125 permitted");
    expect(128 - firstNote(slots, 4) == 3, "only 3 notes remain at MIDI 125");
    slots[4].highKey = 126;
    expect(firstNote(slots, 4) == 131, "custom destination falls back to previous anchor with gap");
    slots[0] = {true, 60, 65, 60};
    slots[4] = {};
    expect(firstNote(slots, 4) == 60, "no valid chromatic anchor means fresh C4");

    slots[2] = {true, std::numeric_limits<double>::quiet_NaN(), 60, 60};
    expect(chromaticNote(slots[2]) == -1, "invalid parameter values cannot be anchors");
    // TEST181: two free destinations, then occupied. N relocates the third
    // SOURCE rather than dropping it. Subsequent files continue after it.
    using lsampler_chromatic_paste::BatchPastePlanner;
    slots.fill({});
    slots[2] = chromatic(62);
    BatchPastePlanner<24> decline(slots, 0);
    auto d0 = decline.assign(false);
    auto d1 = decline.assign(false);
    expect(d0.slot == 0 && d0.note == 60, "first file in slot1 C4");
    expect(d1.slot == 1 && d1.note == 61, "second file in slot2 C#4");
    expect(decline.needsOverwriteDecision() && decline.pendingSlot() == 2, "third file asks before occupied slot3");
    auto d2 = decline.assign(false);  // N
    expect(d2.slot == 3 && d2.note == 63 && d2.relocated && !d2.replaced,
        "No preserves slot3 and imports the THIRD file into slot4 note63");
    auto d3 = decline.assign(false);
    expect(d3.slot == 4 && d3.note == 64, "fourth file follows, no lost item");

    // Y explicitly replaces, conserving that destination's existing pitch.
    slots.fill({});
    slots[2] = chromatic(62);
    BatchPastePlanner<24> agree(slots, 0);
    expect(agree.assign(false).note == 60 && agree.assign(false).note == 61,
        "first two planned before overwrite dialog");
    expect(agree.needsOverwriteDecision(), "Yes prompt for third slot");
    const auto yes = agree.assign(true);
    expect(yes.slot == 2 && yes.note == 62 && yes.replaced && !yes.relocated,
        "Yes replaces occupied third slot at its original chromatic note");
    expect(agree.assign(false).slot == 3, "Yes continues at next sequential slot");

    // Several occupied slots after a No are left untouched and not prompted
    // again while looking for the next free destination.
    slots.fill({});
    slots[2] = chromatic(62);
    slots[3] = chromatic(63);
    BatchPastePlanner<24> consecutive(slots, 0);
    consecutive.assign(false);
    consecutive.assign(false);
    expect(consecutive.needsOverwriteDecision(), "ask for first occupied slot");
    const auto jump = consecutive.assign(false);
    expect(jump.slot == 4 && jump.note == 64 && jump.relocated && jump.protectedSlots == 2,
        "No jumps two occupied slots and finds first unused key");
    expect(consecutive.assign(false).slot == 5, "next item resumes after relocation");

    // An existing later slot may own a key that otherwise would be chosen:
    // the first genuinely unused MIDI note, not raw slot offset, is selected.
    slots.fill({});
    slots[0] = chromatic(60);
    slots[1] = chromatic(61);
    slots[2] = chromatic(62);
    slots[4] = chromatic(64);
    BatchPastePlanner<24> keys(slots, 3);
    const auto key1 = keys.assign(false);
    expect(key1.slot == 3 && key1.note == 63, "use first free key after previous slot");
    expect(keys.needsOverwriteDecision(), "next source encounters occupied slot5");
    const auto key2 = keys.assign(false);
    expect(key2.slot == 5 && key2.note == 65,
        "No preserves note64 and moves current source to next free slot with note65");

    // Even edited key zones count as occupied note ranges. Do not accidentally
    // create overlapping key zones when user kept a custom slot.
    slots.fill({});
    slots[0] = chromatic(60);
    slots[4] = {true, 62, 66, 64};
    BatchPastePlanner<24> custom(slots, 1);
    expect(custom.assign(false).note == 61, "custom range does not block free 61");
    expect(custom.assign(false).note == 67, "custom 62..66 range skips all used notes");

    // If no later free destination exists, do not overwrite without consent.
    slots.fill({});
    slots[23] = chromatic(83);
    BatchPastePlanner<24> full(slots, 23);
    expect(full.needsOverwriteDecision(), "occupied final slot needs consent");
    expect(!full.assign(false), "No at final slot never overwrites or claims import");
    expect(full.pendingSlot() == 23, "failed No leaves planner state unchanged");
    expect(full.assign(true).slot == 23, "Yes can still replace final slot");

    // A full one-note chromatic bank has no free note for another audio file;
    // no clamping to 127 and no duplicate pitches.
    slots.fill({});
    slots[0] = chromatic(127);
    BatchPastePlanner<24> noKey(slots, 1);
    expect(!noKey.assign(false), "MIDI note 128 cannot be imported");

    std::cout << "PASS TEST181: continued chromatic mapping, per-slot Y/N, relocation, no dropped samples, key gaps, MIDI bounds\n";
}
