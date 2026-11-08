#pragma once
// TEST180: deterministic Alt+V pitch mapping for both REAPER item selections
// and Windows Explorer CF_HDROP files. No per-session counter is persisted:
// the saved Low/High/Original Pitch values are the only source of truth.
// This header deliberately has no JUCE/REAPER dependency for regression tests.
#include <array>
#include <cmath>
#include <cstddef>

namespace lsampler_chromatic_paste {

struct SlotMapping
{
    bool hasAudio = false;
    double lowKey = 0.0;
    double highKey = 127.0;
    double originalPitch = 60.0;
};

inline int chromaticNote(const SlotMapping& slot) noexcept
{
    if (!slot.hasAudio || !std::isfinite(slot.lowKey)
        || !std::isfinite(slot.highKey) || !std::isfinite(slot.originalPitch))
        return -1;
    // An edited multi-note key range must not become the anchor of a
    // new one-note-per-slot chromatic import.
    const double note = std::round(slot.originalPitch);
    if (note < 0.0 || note > 127.0
        || std::abs(slot.lowKey - note) > 0.0001
        || std::abs(slot.highKey - note) > 0.0001
        || std::abs(slot.originalPitch - note) > 0.0001)
        return -1;
    return static_cast<int>(note);
}

template <std::size_t N>
int firstNote(const std::array<SlotMapping, N>& slots, int firstSlot) noexcept
{
    if (firstSlot < 0 || static_cast<std::size_t>(firstSlot) >= N)
        return 60;
    // Replacing an already chromatically mapped slot keeps its pitch.
    if (const int existing = chromaticNote(slots[static_cast<std::size_t>(firstSlot)]);
        existing >= 0)
        return existing;

    // The closest earlier chromatically mapped slot anchors the grid. Gaps
    // consume chromatic steps: slot 4 at 63 -> empty slot 8 starts at 67.
    for (int previous = firstSlot - 1; previous >= 0; --previous)
        if (const int note = chromaticNote(slots[static_cast<std::size_t>(previous)]);
            note >= 0)
            return note + (firstSlot - previous);

    // No valid predecessor: preserve the historical first import at MIDI 60.
    return 60;
}



// TEST181: Plan a multi-file paste on the message thread before decoding.
// A declined overwrite MOVES the current file to the first available later
// slot; it does not discard the file or move it to an occupied destination.
// Notes are chosen from the actual occupied key ranges, including customized
// ranges. The next imported sample uses the first free MIDI key at/after the
// previous imported pitch, not the physical distance between destination slots.
struct PasteTarget
{
    int slot = -1;
    int note = -1;
    bool replaced = false;
    bool relocated = false;
    int protectedSlots = 0;
    explicit operator bool() const noexcept { return slot >= 0 && note >= 0; }
};

template <std::size_t N>
class BatchPastePlanner
{
public:
    BatchPastePlanner(const std::array<SlotMapping, N>& initialSlots, int firstSlot) noexcept
        : slots(initialSlots), nextSlot(firstSlot), nextNote(firstNote(initialSlots, firstSlot)) {}

    int pendingSlot() const noexcept
    {
        return nextSlot >= 0 && static_cast<std::size_t>(nextSlot) < N ? nextSlot : -1;
    }
    bool needsOverwriteDecision() const noexcept
    {
        return pendingSlot() >= 0 && slots[static_cast<std::size_t>(nextSlot)].hasAudio;
    }

    PasteTarget assign(bool overwrite) noexcept
    {
        if (pendingSlot() < 0) return {};
        const int requestedSlot = nextSlot;
        int destination = requestedSlot;
        if (slots[static_cast<std::size_t>(destination)].hasAudio && !overwrite)
        {
            // "No" means: keep the old audio and find a free destination
            // for THIS source, even if intervening slots are also occupied.
            for (++destination; static_cast<std::size_t>(destination) < N; ++destination)
                if (!slots[static_cast<std::size_t>(destination)].hasAudio) break;
            if (static_cast<std::size_t>(destination) >= N) return {};
        }
        const auto previous = slots[static_cast<std::size_t>(destination)];
        const bool replaced = previous.hasAudio;
        int note = -1;
        if (replaced)
        {
            // The user explicitly accepted replacement. Preserve the old
            // chromatic pitch when it does not collide with other slots.
            const int mapped = chromaticNote(previous);
            if (mapped >= 0 && !noteTaken(mapped, destination)) note = mapped;
        }
        if (note < 0) note = firstUnusedNote(nextNote, destination);
        if (note < 0) return {};
        slots[static_cast<std::size_t>(destination)] = {true, double(note), double(note), double(note)};
        nextSlot = destination + 1;
        nextNote = note + 1;
        return {destination, note, replaced, destination != requestedSlot, destination - requestedSlot};
    }

private:
    bool noteTaken(int note, int exceptSlot) const noexcept
    {
        if (note < 0 || note > 127) return true;
        for (int slot = 0; static_cast<std::size_t>(slot) < N; ++slot)
        {
            if (slot == exceptSlot) continue;
            const auto& mapping = slots[static_cast<std::size_t>(slot)];
            if (!mapping.hasAudio) continue;
            if (!std::isfinite(mapping.lowKey) || !std::isfinite(mapping.highKey))
            {
                // Invalid ranges should never be silently assumed free.
                return true;
            }
            if (mapping.lowKey <= double(note) && double(note) <= mapping.highKey)
                return true;
        }
        return false;
    }
    int firstUnusedNote(int from, int exceptSlot) const noexcept
    {
        for (int key = from < 0 ? 0 : from; key <= 127; ++key)
            if (!noteTaken(key, exceptSlot)) return key;
        return -1;
    }

    std::array<SlotMapping, N> slots;
    int nextSlot = 0;
    int nextNote = 60;
};

} // namespace lsampler_chromatic_paste
