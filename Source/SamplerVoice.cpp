#include "SamplerVoice.h"
#include <cmath>

void VoiceBank::prepare(double sampleRate)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    allNotesOff();
}

void VoiceBank::setSlots(const std::array<SlotPlaybackState, slotCount>& newSlots)
{
    const juce::SpinLock::ScopedLockType lock(stateLock);
    slots = newSlots;
    for (auto& voice : voices) voice = {};
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

VoiceBank::Voice* VoiceBank::findMonoVoice(int slotIndex)
{
    for (auto& voice : voices)
        if (voice.active && voice.slot == slotIndex)
            return &voice;
    return nullptr;
}

void VoiceBank::noteOn(int note, float velocity)
{
    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        const auto& slot = slots[(size_t) slotIndex];
        const int low = juce::jmin(slot.lowKey, slot.highKey);
        const int high = juce::jmax(slot.lowKey, slot.highKey);
        if (!slot.sample || slot.sample->audio.getNumSamples() == 0 || note < low || note > high)
            continue;

        Voice* voice = nullptr;
        if (slot.mono)
        {
            voice = findMonoVoice(slotIndex);
            if (voice != nullptr && slot.monoLegato)
            {
                voice->note = note;
                voice->velocity = juce::jlimit(0.0f, 1.0f, velocity);
                const double pitchRatio = std::pow(2.0, (note - slot.originalNote) / 12.0);
                voice->increment = (slot.sample->sourceSampleRate / hostSampleRate) * pitchRatio;
                voice->age = ++ageCounter;
                continue;
            }
        }

        if (voice == nullptr)
            voice = &chooseVoice();

        voice->active = true;
        voice->slot = slotIndex;
        voice->note = note;
        voice->position = 0.0;
        voice->age = ++ageCounter;
        voice->sample = slot.sample;
        voice->velocity = juce::jlimit(0.0f, 1.0f, velocity);
        const double pitchRatio = std::pow(2.0, (note - slot.originalNote) / 12.0);
        voice->increment = (slot.sample->sourceSampleRate / hostSampleRate) * pitchRatio;
    }
}

void VoiceBank::noteOff(int note)
{
    for (auto& voice : voices)
        if (voice.active && voice.note == note)
            voice.active = false;
}

void VoiceBank::allNotesOff()
{
    const juce::SpinLock::ScopedLockType lock(stateLock);
    for (auto& voice : voices)
        voice = {};
}

void VoiceBank::render(juce::AudioBuffer<float>& output, juce::MidiBuffer& midi)
{
    const juce::SpinLock::ScopedLockType lock(stateLock);
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            noteOn(message.getNoteNumber(), message.getFloatVelocity());
        else if (message.isNoteOff())
            noteOff(message.getNoteNumber());
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            allNotesOff();
    }

    for (auto& voice : voices)
    {
        if (!voice.active || !voice.sample)
            continue;

        const auto& slot = slots[(size_t) juce::jlimit(0, slotCount - 1, voice.slot)];
        const int sourceSamples = voice.sample->audio.getNumSamples();
        const int sourceChannels = voice.sample->audio.getNumChannels();
        if (sourceSamples < 2 || sourceChannels == 0)
        {
            voice.active = false;
            continue;
        }

        for (int outSample = 0; outSample < output.getNumSamples(); ++outSample)
        {
            const int i0 = static_cast<int>(voice.position);
            if (i0 >= sourceSamples - 1)
            {
                voice.active = false;
                break;
            }

            const int i1 = i0 + 1;
            const float frac = static_cast<float>(voice.position - i0);
            const float voiceGain = slot.gain * voice.velocity;

            for (int ch = 0; ch < output.getNumChannels(); ++ch)
            {
                const int srcCh = sourceChannels == 1 ? 0 : juce::jmin(ch, sourceChannels - 1);
                const float a = voice.sample->audio.getSample(srcCh, i0);
                const float b = voice.sample->audio.getSample(srcCh, i1);
                output.addSample(ch, outSample, (a + (b - a) * frac) * voiceGain);
            }
            voice.position += voice.increment;
        }
    }
}
