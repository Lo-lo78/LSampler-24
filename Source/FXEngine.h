#pragma once
#include "FXParameters.h"
#include "FXReverb.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

// Real-time safe, pre-allocated stereo master bus FX. All writes belong to the
// audio thread; UI and VST3 automation communicate through atomic HostParameters.
// Chorus follows LJuno-116 TEST85's stereo quadrature delay and high-pass path.
// Dual Delay follows TEST85 Delay 2's twin tape speeds / glide / tone / drive.
class LSamplerFXEngine {
public:
    void prepare(double rate) {
        sampleRate = std::max(8000.0, rate);
        chorusL.assign(size_t(sampleRate * .035) + 8, 0.0f);
        chorusR.assign(chorusL.size(), 0.0f);
        tapeL.assign(size_t(sampleRate * 7.5) + 8, 0.0f);
        tapeR.assign(tapeL.size(), 0.0f);
        predelayL.assign(size_t(sampleRate * .45) + 8, 0.0f);
        predelayR.assign(predelayL.size(), 0.0f);
        reverbNetwork.prepare(sampleRate);
        reset();
    }
    void reset() noexcept {
        std::fill(chorusL.begin(), chorusL.end(), 0.0f);
        std::fill(chorusR.begin(), chorusR.end(), 0.0f);
        std::fill(tapeL.begin(), tapeL.end(), 0.0f);
        std::fill(tapeR.begin(), tapeR.end(), 0.0f);
        std::fill(predelayL.begin(), predelayL.end(), 0.0f);
        std::fill(predelayR.begin(), predelayR.end(), 0.0f);
        chorusWrite = tapeWrite = preWrite = 0;
        phase = smoothedRate = timeL = timeR = 0;
        hpLX = hpLY = hpRX = hpRY = 0;
        tapeDCX[0] = tapeDCX[1] = tapeDCY[0] = tapeDCY[1] = 0;
        low[0] = low[1] = hiX[0] = hiX[1] = hiY[0] = hiY[1] = 0;
        tailLow[0] = tailLow[1] = 0;
        resonantLow[0] = resonantLow[1] = resonantBand[0] = resonantBand[1] = 0;
        gateLevel = 0.0f;
        pan1L=.9807852804f;pan1R=.1950903220f;
        pan2L=.1950903220f;pan2R=.9807852804f;
        reverbNetwork.reset();
        tailFrames = 0;
    }
    bool hasTail() const noexcept { return tailFrames > 0; }
    void process(juce::AudioBuffer<float>& buffer, const std::array<float, lsampler::fx::count>& p,
                 const std::array<juce::AudioBuffer<float>,3>* slotSends = nullptr, double bpm = 120.0) noexcept {
        using namespace lsampler::fx;
        if (p[master] < .5f) { if (tailFrames > 0) reset(); return; }
        const bool chorus = p[chorusOn] > .5f && p[chorusMix] > .001f;
        const bool delay = p[delayOn] > .5f && p[delayMix] > .001f;
        const int mode = juce::jlimit(0, 4, int(std::round(p[reverbType])));
        const bool reverb = mode > 0 && p[reverbMix] > .001f;
        if ((!chorus && !delay && !reverb) || buffer.getNumChannels() < 2) {
            if (tailFrames > 0) reset();
            return;
        }
        auto* outL = buffer.getWritePointer(0);
        auto* outR = buffer.getWritePointer(1);
        const int count = buffer.getNumSamples();
        // The wet algorithms are entirely silent until actual nonzero input;
        // zero buffers never start/retrigger DSP work in idle.
        bool input = false;
        if (slotSends == nullptr)
        {
            for (int i = 0; i < count; ++i)
                if (std::abs(outL[i]) > 1.e-9f || std::abs(outR[i]) > 1.e-9f)
                { input = true; break; }
        }
        else
        {
            // Dry notes with all three per-Slot Sends at zero must not wake
            // the FX engines or manufacture a long silent reverb tail.
            for (int send = 0; send < 3 && !input; ++send)
            {
                if ((send == 0 && !chorus) || (send == 1 && !delay) || (send == 2 && !reverb)) continue;
                for (int i = 0; i < count; ++i)
                    if (std::abs((*slotSends)[size_t(send)].getSample(0, i)) > 1.e-9f
                        || std::abs((*slotSends)[size_t(send)].getSample(1, i)) > 1.e-9f)
                    { input = true; break; }
            }
        }
        if (!input && tailFrames <= 0) return;
        constexpr std::array<int,8> divisions {0,32,24,16,12,8,6,4};
        const int syncIndex = juce::jlimit(0,7,int(std::round(p[delaySync])));
        const double safeBpm = std::isfinite(bpm) && bpm > 0 ? bpm : 120.0;
        const double baseSeconds = syncIndex == 0
            ? double(p[delayTime]) * .001
            : 240.0 / (safeBpm * double(divisions[size_t(syncIndex)]));
        if (input) {
            // Do not run twelve seconds of silent DSP for a chorus-only note.
            // The active time-based effects alone determine the tail budget.
            const double delaySeconds = delay
                ? 1.0 + std::min(baseSeconds * std::max(double(p[delaySpeedL]),double(p[delaySpeedR])),
                    double(tapeL.size()-4) / sampleRate)
                    * (2.0 + 9.0 * std::min(.99,double(std::max(p[delayFeedbackL],p[delayFeedbackR]))*.5))
                : 0.0;
            // A gated tail can end sooner than the ungated RT60. Legacy default
            // Threshold -80 (= Off) uses the original untouched budget.
            const double reverbSeconds = !reverb ? 0.0 : p[reverbGateThreshold] <= -79.5f
                ? .45 + .001 * double(p[reverbDecay])
                : .45 + .001 * double(p[reverbPredelay] + p[reverbGateDecay]);
            tailFrames = juce::roundToInt(juce::jlimit(.12, 30.0,
                std::max({chorus ? .12 : 0.0,delaySeconds,reverbSeconds})) * sampleRate);
        } else tailFrames = std::max(0, tailFrames - count);

        const float chorusGain = p[chorusMix] * .01f * (.5f + .5f*p[chorusWidth]);
        const float rate = p[chorusRate];
        const float delayMixGain = p[delayMix] * .01f * std::pow(10.0f, p[delayGain] / 20.0f); // additive send, dry stays unity
        const float wetReverb = p[reverbMix] * .01f * std::pow(10.0f, p[reverbGain] / 20.0f);
        const float spread = p[delaySpread];
        const float drive = p[delayDrive] * .01f;
        const float drive2 = drive*drive;
        const float driveGain = 1.0f + 79.0f*drive2;
        const float driveBias = .185f * drive2;
        const float driveZeroSoft = driveBias/(1.0f+std::abs(driveBias));
        const float driveZero = driveZeroSoft*(1.0f-drive2)+driveBias*drive2;
        // Note-length divisions follow the DAW tempo. Off preserves the
        // original millisecond delay precisely, including tape speed/glide.
        const double tBase = baseSeconds * sampleRate;
        const float levelL = p[delayLevelL] <= -95.5f ? 0.0f : std::pow(10.0f, p[delayLevelL] / 20.0f);
        const float levelR = p[delayLevelR] <= -95.5f ? 0.0f : std::pow(10.0f, p[delayLevelR] / 20.0f);
        const float glide = float(1.0-std::exp(-1.0/(sampleRate*std::max(.001, double(p[delayGlide])*.001))));
        const float targetL = juce::jlimit(16.0f, float(tapeL.size()-4), float(tBase*p[delaySpeedL]));
        const float targetR = juce::jlimit(16.0f, float(tapeR.size()-4), float(tBase*p[delaySpeedR]));
        if (timeL <= 0) timeL = targetL;
        if (timeR <= 0) timeR = targetR;
        const float hpCoefficient = float(std::exp(-juce::MathConstants<double>::twoPi*120.0/sampleRate));
        // Calculate resonance coefficients once per block, not per sample.
        // Default resonance 0 skips this work and keeps the legacy filters.
        std::array<float,2> resonanceAmount {}, resonanceCoeff {};
        for (int channel = 0; channel < 2; ++channel) {
            resonanceAmount[size_t(channel)] = .01f * p[channel == 0 ? delayResonanceL : delayResonanceR];
            const float control = p[channel == 0 ? delayToneL : delayToneR];
            if (resonanceAmount[size_t(channel)] > 0.0f && std::abs(control - .5f) > .0001f) {
                const double cutoff = control < .5f ? 600.0 + 7000.0 * control / .5
                                                     : 40.0 + 1800.0 * (control - .5) / 1.5;
                resonanceCoeff[size_t(channel)] = juce::jlimit(.002f, .8f,
                    float(2.0 * std::sin(juce::MathConstants<double>::pi * cutoff / sampleRate)));
            }
        }
        // Gentle parallel band-pass emphasis around each Tone cutoff.
        const auto tone = [this, &resonanceAmount, &resonanceCoeff](float x, float control, int channel) noexcept -> float {
            float filtered = x;
            double cutoff = 0.0;
            if (control < .4999f) {
                cutoff=600.0 + 7000.0*control/.5;
                const float c = float(1.0-std::exp(-juce::MathConstants<double>::twoPi*cutoff/sampleRate));
                low[channel] += (x-low[channel])*c;
                hiX[channel] = hiY[channel] = low[channel];
                filtered = low[channel];
            }
            else if (control > .5001f) {
                cutoff=40.0 + 1800.0*(control-.5)/1.5;
                const float c=float(std::exp(-juce::MathConstants<double>::twoPi*cutoff/sampleRate));
                const float y=x-hiX[channel]+c*hiY[channel];
                hiX[channel]=x; hiY[channel]=y; low[channel]=x;
                filtered = y;
            }
            else low[channel] = hiX[channel] = hiY[channel] = x;
            const float resonance = resonanceAmount[size_t(channel)];
            if (resonance <= 0.0f || cutoff <= 0.0) return filtered;
            // Bounded two-integrator bandpass. No trigonometric calls per sample.
            const float f = resonanceCoeff[size_t(channel)];
            resonantBand[channel] += f * (x - resonantLow[channel] - .85f * resonantBand[channel]);
            resonantLow[channel] += f * resonantBand[channel];
            return filtered + .35f * resonance * resonantBand[channel];
        };
        const auto shape = [=](float x) noexcept {
            const float u = juce::jlimit(-16.0f,16.0f,x*driveGain+driveBias);
            const float a=std::abs(u);
            const float soft=u >= 0 ? u/(1.0f+a) : .78f*u/(.78f+a);
            const float hard=juce::jlimit(-1.0f,1.0f,u);
            return x+((soft*(1.0f-drive2)+hard*drive2-driveZero)-x)*drive;
        };
        if (reverb) reverbNetwork.configure(p, mode);
        const int preFrames = juce::jlimit(0, int(predelayL.size())-2, juce::roundToInt(p[reverbPredelay]*.001*sampleRate));
        const float early = p[reverbEarly]*.01f;
        // Audible tone from ~350 Hz to ~18 kHz, computed only per audio block.
        const double toneHz = 350.0 * std::pow(18000.0 / 350.0, double(p[reverbTone]) * .01);
        const float highTone = float(1.0-std::exp(-juce::MathConstants<double>::twoPi*toneHz/sampleRate));
        const float roomSize = p[reverbSize] * .01f;
        // ER spacings grow with room size and vary across room algorithms.
        const float modeFactor = mode==1 ? .50f : mode==2 ? 1.0f : mode==3 ? .75f : 1.3f;
        const float earlySpacing = float(sampleRate * .001 * modeFactor * (3.0 + 22.0*roomSize));
        // Gate values are block controls: never call pow() in the audio sample loop.
        const bool gateEnabled = p[reverbGateThreshold] > -79.5f;
        const float gateThresholdAmp = gateEnabled
            ? std::pow(10.0f, p[reverbGateThreshold] / 20.0f) : 0.0f;
        const float gateDecaySamples = float(sampleRate * .001 * p[reverbGateDecay]);
        const float gateDecayStep = gateDecaySamples <= 1.0f ? 1.0f : 1.0f / gateDecaySamples;
        for (int i=0;i<count;++i) {
            const float dryL=outL[i], dryR=outR[i];
            const float cLIn = slotSends ? (*slotSends)[0].getSample(0, i) : dryL;
            const float cRIn = slotSends ? (*slotSends)[0].getSample(1, i) : dryR;
            const float dLIn = slotSends ? (*slotSends)[1].getSample(0, i) : dryL;
            const float dRIn = slotSends ? (*slotSends)[1].getSample(1, i) : dryR;
            const float rLIn = slotSends ? (*slotSends)[2].getSample(0, i) : dryL;
            const float rRIn = slotSends ? (*slotSends)[2].getSample(1, i) : dryR;
            float l=dryL, r=dryR;
            if (chorus) {
                smoothedRate += (rate-smoothedRate)*.0005f;
                phase += (.10 + smoothedRate*1.40)/sampleRate;
                if (phase >= 1) phase -= 1;
                const double a=std::sin(juce::MathConstants<double>::twoPi*phase);
                const double b=std::sin(juce::MathConstants<double>::twoPi*std::fmod(phase+.25,1.0));
                const float dL=float((.006 + .012*(.5+.5*a*.65))*sampleRate);
                const float dR=float((.006 + .012*(.5+.5*b*.65))*sampleRate);
                const float cL=read(chorusL,chorusWrite,dL);
                const float cR=read(chorusR,chorusWrite,dR);
                chorusL[size_t(chorusWrite)]=cLIn;
                chorusR[size_t(chorusWrite)]=cRIn;
                chorusWrite=(chorusWrite+1)%int(chorusL.size());
                const float hL=cL-hpLX+hpCoefficient*hpLY;
                const float hR=cR-hpRX+hpCoefficient*hpRY;
                hpLX=cL; hpLY=hL; hpRX=cR; hpRY=hR;
                l += hL*chorusGain;
                r += hR*chorusGain;
            }
            if (delay) {
                timeL += (targetL-timeL)*glide;
                timeR += (targetR-timeR)*glide;
                const float rawL=read(tapeL,tapeWrite,timeL);
                const float rawR=read(tapeR,tapeWrite,timeR);
                const float wetL=rawL-tapeDCX[0]+.995f*tapeDCY[0];
                const float wetR=rawR-tapeDCX[1]+.995f*tapeDCY[1];
                tapeDCX[0]=rawL; tapeDCY[0]=wetL;
                tapeDCX[1]=rawR; tapeDCY[1]=wetR;
                const float fbL=juce::jlimit(-2.0f,2.0f,tone(rawL,p[delayToneL],0)*p[delayFeedbackL]*.4999f);
                const float fbR=juce::jlimit(-2.0f,2.0f,tone(rawR,p[delayToneR],1)*p[delayFeedbackR]*.4999f);
                const float monoIn=.5f*(dLIn+dRIn);
                tapeL[size_t(tapeWrite)]=juce::jlimit(-4.0f,4.0f,monoIn + fbL);
                tapeR[size_t(tapeWrite)]=juce::jlimit(-4.0f,4.0f,monoIn + fbR);
                tapeWrite=(tapeWrite+1)%int(tapeL.size());
                const float wL=shape(wetL)*levelL, wR=shape(wetR)*levelR;
                // LJuno TEST85 LWS-7 equal-power stereo spread and smoothed pans.
                const float angle1=(-spread+1.0f)*juce::MathConstants<float>::pi*.25f;
                const float angle2=(spread+1.0f)*juce::MathConstants<float>::pi*.25f;
                const float panCoeff=float(1.0-std::exp(-1.0/(.005*sampleRate)));
                pan1L+=(std::cos(angle1)-pan1L)*panCoeff;
                pan1R+=(std::sin(angle1)-pan1R)*panCoeff;
                pan2L+=(std::cos(angle2)-pan2L)*panCoeff;
                pan2R+=(std::sin(angle2)-pan2R)*panCoeff;
                // TEST144 applied an extra -9 dB (.3535) to the already
                // mono-summed send. A full send was much quieter than Reverb.
                // Stereo pan vectors are unity power; send=100 now reaches an
                // appropriately strong echo without attenuating the dry path.
                constexpr float equalGain=1.25f;
                l+=delayMixGain*(wL*pan1L+wR*pan2L)*equalGain;
                r+=delayMixGain*(wL*pan1R+wR*pan2R)*equalGain;
            }
            if (reverb) {
                // Predelay, early reflections and a stereo FDN. No heap
                // allocation or lock in the sample callback.
                // Per-slot sends are independent: with Reverb Send 0, this
                // slot must not reach the reverb through chorus/delay returns.
                // Legacy fallback retains the original serial FX topology.
                predelayL[size_t(preWrite)]=slotSends ? rLIn : l;
                predelayR[size_t(preWrite)]=slotSends ? rRIn : r;
                const float preL=read(predelayL,preWrite,float(preFrames));
                const float preR=read(predelayR,preWrite,float(preFrames));
                // Six decorrelated, adjustable early reflections. 0% removes
                // early reflections entirely; 100% emphasises a convincing
                // first-bounce pattern without changing the late RT60.
                const float e1L=read(predelayL,preWrite,float(preFrames)+earlySpacing);
                const float e1R=read(predelayR,preWrite,float(preFrames)+earlySpacing*1.13f);
                const float e2L=read(predelayR,preWrite,float(preFrames)+earlySpacing*2.37f);
                const float e2R=read(predelayL,preWrite,float(preFrames)+earlySpacing*2.87f);
                const float e3L=read(predelayL,preWrite,float(preFrames)+earlySpacing*3.73f);
                const float e3R=read(predelayR,preWrite,float(preFrames)+earlySpacing*4.19f);
                preWrite=(preWrite+1)%int(predelayL.size());
                const float earlyL=early*(.48f*e1L+.35f*e2L-.27f*e3L);
                const float earlyR=early*(.48f*e1R+.35f*e2R-.27f*e3R);
                float tailL=0.0f,tailR=0.0f;
                // Only the late network gets predelayed direct input. ER is a
                // separately controlled send, not a hidden change to decay.
                reverbNetwork.process(preL, preR, tailL, tailR);
                tailLow[0] += highTone*(tailL-tailLow[0]);
                tailLow[1] += highTone*(tailR-tailLow[1]);
                float revL=tailLow[0]*1.80f+earlyL;
                float revR=tailLow[1]*1.80f+earlyR;
                // Instant gate opening, decay-only closing. The threshold is
                // measured on the pre-FX reverb send to avoid reverb self-keying.
                // -80 dB is an explicit Off sentinel: old banks sound identical.
                if (gateEnabled) {
                    if (std::max(std::abs(rLIn), std::abs(rRIn)) >= gateThresholdAmp)
                        gateLevel = 1.0f;
                    else gateLevel = std::max(0.0f, gateLevel - gateDecayStep);
                    revL *= gateLevel;
                    revR *= gateLevel;
                } else gateLevel = 1.0f;
                // 0% = mono room; 100% = original decorrelated stereo.
                const float mid=.5f*(revL+revR);
                const float side=.5f*(revL-revR)*(p[reverbWidth]*.01f);
                l += wetReverb*(mid+side);
                r += wetReverb*(mid-side);
            }
            outL[i]=std::isfinite(l)?l:0;
            outR[i]=std::isfinite(r)?r:0;
        }
        if (tailFrames <= 0) reset();
    }
private:
    static float read(const std::vector<float>& b,int write,float samples) noexcept {
        if (b.empty()) return 0;
        float pos=float(write)-samples;
        const float n=float(b.size());
        while(pos<0)pos+=n;
        while(pos>=n)pos-=n;
        const int a=int(pos);
        const int c=(a+1)%int(b.size());
        const float frac=pos-float(a);
        return b[size_t(a)] + frac*(b[size_t(c)]-b[size_t(a)]);
    }
    double sampleRate=44100;
    std::vector<float> chorusL, chorusR, tapeL, tapeR, predelayL, predelayR;
    LSamplerFXReverb reverbNetwork;
    int chorusWrite=0,tapeWrite=0,preWrite=0,tailFrames=0;
    double phase=0;
    float smoothedRate=0,timeL=0,timeR=0;
    float pan1L=.9807852804f,pan1R=.1950903220f,pan2L=.1950903220f,pan2R=.9807852804f;
    float hpLX=0,hpLY=0,hpRX=0,hpRY=0;
    float tapeDCX[2]{},tapeDCY[2]{},low[2]{},hiX[2]{},hiY[2]{},tailLow[2]{};
    float resonantLow[2]{}, resonantBand[2]{}, gateLevel=0.0f;
};
