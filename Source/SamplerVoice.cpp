#include "SamplerVoice.h"
#include <cmath>

void VoiceBank::prepare(double sampleRate)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    allNotesOff();
}

VoiceBank::Voice& VoiceBank::chooseVoice()
{
    for (auto& voice : voices)
        if (!voice.active)
            return voice;

    auto* oldest = &voices.front();
    for (auto& voice : voices)
        if (voice.age < oldest->age)
            oldest = &voice;
    return *oldest;
}

VoiceBank::Voice* VoiceBank::findMonoVoice(int slot)
{
    for (auto& voice : voices)
        if (voice.active && voice.slot == slot)
            return &voice;
    return nullptr;
}

void VoiceBank::startVoice(Voice& voice, int slot, int note, float velocity,
                           const SlotRuntimeState& state, bool restart)
{
    if (!state.sample || state.sample->audio.getNumSamples() < 2)
        return;

    voice.active = true;
    voice.note = note;
    voice.slot = slot;
    voice.sample = state.sample;
    if (restart)
        voice.position = 0.0;
    voice.age = ++ageCounter;
    voice.increment = std::pow(2.0, (note - state.originalPitch) / 12.0)
                    * (state.sample->sourceSampleRate / hostSampleRate);
    voice.gain = juce::jlimit(0.0f, 2.0f, state.volume)
               * juce::jlimit(0.0f, 1.0f, velocity);
}

void VoiceBank::noteOn(int note, float velocity, const SamplerRuntimeState& runtime)
{
    note = juce::jlimit(0, 127, note);

    for (int slot = 0; slot < SamplerRuntimeState::slotCount; ++slot)
    {
        const auto& state = runtime.slots[static_cast<size_t>(slot)];
        if (!state.sample || note < state.lowKey || note > state.highKey)
            continue;

        heldNoteOrder[static_cast<size_t>(slot)][static_cast<size_t>(note)] = ++noteOrderCounter;

        if (state.mono)
        {
            if (auto* existing = findMonoVoice(slot))
            {
                startVoice(*existing, slot, note, velocity, state, !state.legato);
                continue;
            }
        }

        auto& voice = chooseVoice();
        startVoice(voice, slot, note, velocity, state, true);
    }
}

int VoiceBank::newestHeldNoteForSlot(int slot, const SlotRuntimeState& state) const
{
    uint64_t newest = 0;
    int result = -1;
    const auto& orders = heldNoteOrder[static_cast<size_t>(slot)];
    for (int note = juce::jlimit(0, 127, state.lowKey);
         note <= juce::jlimit(0, 127, state.highKey); ++note)
    {
        const auto order = orders[static_cast<size_t>(note)];
        if (order > newest)
        {
            newest = order;
            result = note;
        }
    }
    return result;
}

void VoiceBank::noteOff(int note, const SamplerRuntimeState& runtime)
{
    note = juce::jlimit(0, 127, note);

    for (int slot = 0; slot < SamplerRuntimeState::slotCount; ++slot)
    {
        const auto& state = runtime.slots[static_cast<size_t>(slot)];
        heldNoteOrder[static_cast<size_t>(slot)][static_cast<size_t>(note)] = 0;

        if (state.mono)
        {
            auto* monoVoice = findMonoVoice(slot);
            if (monoVoice == nullptr || monoVoice->note != note)
                continue;

            const int fallback = newestHeldNoteForSlot(slot, state);
            if (fallback >= 0 && state.sample)
            {
                startVoice(*monoVoice, slot, fallback, 1.0f, state, !state.legato);
            }
            else
            {
                monoVoice->active = false;
                monoVoice->sample.reset();
            }
            continue;
        }

        for (auto& voice : voices)
        {
            if (voice.active && voice.slot == slot && voice.note == note)
            {
                voice.active = false;
                voice.sample.reset();
            }
        }
    }
}

void VoiceBank::allNotesOff()
{
    for (auto& voice : voices)
        voice = {};
    for (auto& slot : heldNoteOrder)
        slot.fill(0);
}

void VoiceBank::render(juce::AudioBuffer<float>& output, juce::MidiBuffer& midi,
                       const SamplerRuntimeState& runtime)
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            noteOn(message.getNoteNumber(), message.getFloatVelocity(), runtime);
        else if (message.isNoteOff())
            noteOff(message.getNoteNumber(), runtime);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            allNotesOff();
    }

    for (auto& voice : voices)
    {
        if (!voice.active || !voice.sample)
            continue;

        const auto& audio = voice.sample->audio;
        const int sourceSamples = audio.getNumSamples();
        const int sourceChannels = audio.getNumChannels();
        if (sourceSamples < 2 || sourceChannels == 0)
        {
            voice.active = false;
            voice.sample.reset();
            continue;
        }

        for (int outSample = 0; outSample < output.getNumSamples(); ++outSample)
        {
            const int i0 = static_cast<int>(voice.position);
            if (i0 < 0 || i0 >= sourceSamples - 1)
            {
                voice.active = false;
                voice.sample.reset();
                break;
            }

            const int i1 = i0 + 1;
            const float frac = static_cast<float>(voice.position - static_cast<double>(i0));
            for (int ch = 0; ch < output.getNumChannels(); ++ch)
            {
                const int srcCh = sourceChannels == 1 ? 0 : juce::jmin(ch, sourceChannels - 1);
                const float a = audio.getSample(srcCh, i0);
                const float b = audio.getSample(srcCh, i1);
                output.addSample(ch, outSample, (a + (b - a) * frac) * voice.gain);
            }
            voice.position += voice.increment;
        }
    }
}
