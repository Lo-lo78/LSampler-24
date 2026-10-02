#include "SamplerVoice.h"
#include <cmath>

void VoiceBank::prepare(double sampleRate)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    allNotesOff();
}

void VoiceBank::setSample(std::shared_ptr<SharedSample> newSample)
{
    sample = std::move(newSample);
    allNotesOff();
}

void VoiceBank::setRootNote(int midiNote)
{
    rootNote = juce::jlimit(0, 127, midiNote);
}

void VoiceBank::setGain(float linearGain)
{
    gain = juce::jlimit(0.0f, 2.0f, linearGain);
}

VoiceBank::Voice& VoiceBank::chooseVoice()
{
    for (auto& v : voices)
        if (!v.active)
            return v;

    auto* oldest = &voices.front();
    for (auto& v : voices)
        if (v.age < oldest->age)
            oldest = &v;
    return *oldest;
}

void VoiceBank::noteOn(int note, float velocity)
{
    if (!sample || sample->audio.getNumSamples() == 0)
        return;

    auto& voice = chooseVoice();
    const auto idx = static_cast<size_t>(&voice - voices.data());
    voice.active = true;
    voice.note = note;
    voice.position = 0.0;
    voice.age = ++ageCounter;
    voice.increment = std::pow(2.0, (note - rootNote) / 12.0)
                    * (sample->sourceSampleRate / hostSampleRate);
    velocityGain[idx] = juce::jlimit(0.0f, 1.0f, velocity);
}

void VoiceBank::noteOff(int note)
{
    for (auto& voice : voices)
        if (voice.active && voice.note == note)
            voice.active = false;
}

void VoiceBank::allNotesOff()
{
    for (auto& voice : voices)
        voice = {};
}

void VoiceBank::render(juce::AudioBuffer<float>& output, juce::MidiBuffer& midi)
{
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

    if (!sample)
        return;

    const int sourceSamples = sample->audio.getNumSamples();
    const int sourceChannels = sample->audio.getNumChannels();
    if (sourceSamples < 2 || sourceChannels == 0)
        return;

    for (size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
    {
        auto& voice = voices[voiceIndex];
        if (!voice.active)
            continue;

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
            const float voiceGain = gain * velocityGain[voiceIndex];

            for (int ch = 0; ch < output.getNumChannels(); ++ch)
            {
                const int srcCh = sourceChannels == 1 ? 0 : juce::jmin(ch, sourceChannels - 1);
                const float a = sample->audio.getSample(srcCh, i0);
                const float b = sample->audio.getSample(srcCh, i1);
                output.addSample(ch, outSample, (a + (b - a) * frac) * voiceGain);
            }

            voice.position += voice.increment;
        }
    }
}
