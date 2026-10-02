#include "SamplerVoice.h"
#include <cmath>

void GlobalVoicePool::prepare(double sampleRate)
{
    hostSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    allNotesOff();
}

GlobalVoicePool::Voice& GlobalVoicePool::chooseVoice()
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

void GlobalVoicePool::noteOn(int slotIndex, int note, float velocity,
                             std::shared_ptr<SharedSample> sampleToUse,
                             int rootNote, float linearGain, bool isPreview)
{
    if (!sampleToUse || sampleToUse->audio.getNumSamples() < 2)
        return;

    auto& voice = chooseVoice();
    voice = {};
    voice.active = true;
    voice.preview = isPreview;
    voice.slotIndex = slotIndex;
    voice.note = juce::jlimit(0, 127, note);
    voice.position = 0.0;
    voice.age = ++ageCounter;
    voice.sample = std::move(sampleToUse);
    voice.gain = juce::jlimit(0.0f, 2.0f, linearGain)
               * juce::jlimit(0.0f, 1.0f, velocity);
    voice.increment = std::pow(2.0, (voice.note - juce::jlimit(0, 127, rootNote)) / 12.0)
                    * (voice.sample->sourceSampleRate / hostSampleRate);
}

void GlobalVoicePool::noteOff(int note)
{
    for (auto& voice : voices)
        if (voice.active && !voice.preview && voice.note == note)
            voice.active = false;
}

void GlobalVoicePool::allNotesOff()
{
    for (auto& voice : voices)
        voice = {};
}

void GlobalVoicePool::stopPreviewVoices(int slotIndex)
{
    for (auto& voice : voices)
        if (voice.active && voice.preview && (slotIndex < 0 || voice.slotIndex == slotIndex))
            voice = {};
}

bool GlobalVoicePool::hasPreviewVoices(int slotIndex) const noexcept
{
    for (const auto& voice : voices)
        if (voice.active && voice.preview && (slotIndex < 0 || voice.slotIndex == slotIndex))
            return true;
    return false;
}

void GlobalVoicePool::render(juce::AudioBuffer<float>& output)
{
    for (auto& voice : voices)
    {
        if (!voice.active || !voice.sample)
            continue;

        const int sourceSamples = voice.sample->audio.getNumSamples();
        const int sourceChannels = voice.sample->audio.getNumChannels();
        if (sourceSamples < 2 || sourceChannels == 0)
        {
            voice = {};
            continue;
        }

        for (int outSample = 0; outSample < output.getNumSamples(); ++outSample)
        {
            const int i0 = static_cast<int>(voice.position);
            if (i0 >= sourceSamples - 1)
            {
                voice = {};
                break;
            }

            const int i1 = i0 + 1;
            const float frac = static_cast<float>(voice.position - i0);

            for (int ch = 0; ch < output.getNumChannels(); ++ch)
            {
                const int srcCh = sourceChannels == 1 ? 0 : juce::jmin(ch, sourceChannels - 1);
                const float a = voice.sample->audio.getSample(srcCh, i0);
                const float b = voice.sample->audio.getSample(srcCh, i1);
                output.addSample(ch, outSample, (a + (b - a) * frac) * voice.gain);
            }

            voice.position += voice.increment;
        }
    }
}
