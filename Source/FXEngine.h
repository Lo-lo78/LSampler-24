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
        pan1L=.9807852804f;pan1R=.1950903220f;
        pan2L=.1950903220f;pan2R=.9807852804f;
        reverbNetwork.reset();
        tailFrames = 0;
    }
    bool hasTail() const noexcept { return tailFrames > 0; }
    void process(juce::AudioBuffer<float>& buffer, const std::array<float, lsampler::fx::count>& p) noexcept {
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
        for (int i=0; i<count; ++i)
            if (std::abs(outL[i]) > 1.e-9f || std::abs(outR[i]) > 1.e-9f) { input=true; break; }
        if (!input && tailFrames <= 0) return;
        if (input) {
            // Do not run twelve seconds of silent DSP for a chorus-only note.
            // The active time-based effects alone determine the tail budget.
            const double delaySeconds = delay
                ? 1.0 + .001 * double(p[delayTime]) * std::max(double(p[delaySpeedL]),double(p[delaySpeedR]))
                    * (2.0 + 9.0 * std::min(.99,double(std::max(p[delayFeedbackL],p[delayFeedbackR]))*.5))
                : 0.0;
            const double reverbSeconds = reverb ? .45 + .001 * double(p[reverbDecay]) : 0.0;
            tailFrames = juce::roundToInt(juce::jlimit(.12, 30.0,
                std::max({chorus ? .12 : 0.0,delaySeconds,reverbSeconds})) * sampleRate);
        } else tailFrames = std::max(0, tailFrames - count);

        const float chorusGain = p[chorusMix] * .01f * (.5f + .5f*p[chorusWidth]);
        const float rate = p[chorusRate];
        const float delayMixGain = p[delayMix] * .01f; // additive send, dry stays unity
        const float wetReverb = p[reverbMix] * .01f;
        const float spread = p[delaySpread];
        const float drive = p[delayDrive] * .01f;
        const float drive2 = drive*drive;
        const float driveGain = 1.0f + 79.0f*drive2;
        const float driveBias = .185f * drive2;
        const float driveZeroSoft = driveBias/(1.0f+std::abs(driveBias));
        const float driveZero = driveZeroSoft*(1.0f-drive2)+driveBias*drive2;
        const double tBase = p[delayTime] * .001 * sampleRate;
        const float glide = float(1.0-std::exp(-1.0/(sampleRate*std::max(.001, double(p[delayGlide])*.001))));
        const float targetL = juce::jlimit(16.0f, float(tapeL.size()-4), float(tBase*p[delaySpeedL]));
        const float targetR = juce::jlimit(16.0f, float(tapeR.size()-4), float(tBase*p[delaySpeedR]));
        if (timeL <= 0) timeL = targetL;
        if (timeR <= 0) timeR = targetR;
        const float hpCoefficient = float(std::exp(-juce::MathConstants<double>::twoPi*120.0/sampleRate));
        const auto tone = [this](float x, float control, int channel) noexcept -> float {
            if (control < .4999f) {
                const double cutoff=600.0 + 7000.0*control/.5;
                const float c = float(1.0-std::exp(-juce::MathConstants<double>::twoPi*cutoff/sampleRate));
                low[channel] += (x-low[channel])*c;
                hiX[channel] = hiY[channel] = low[channel];
                return low[channel];
            }
            if (control > .5001f) {
                const double cutoff=40.0 + 1800.0*(control-.5)/1.5;
                const float c=float(std::exp(-juce::MathConstants<double>::twoPi*cutoff/sampleRate));
                const float y=x-hiX[channel]+c*hiY[channel];
                hiX[channel]=x; hiY[channel]=y; low[channel]=x;
                return y;
            }
            low[channel] = hiX[channel] = hiY[channel] = x;
            return x;
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
        for (int i=0;i<count;++i) {
            const float dryL=outL[i], dryR=outR[i];
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
                chorusL[size_t(chorusWrite)]=dryL;
                chorusR[size_t(chorusWrite)]=dryR;
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
                const float monoIn=.5f*(dryL+dryR);
                tapeL[size_t(tapeWrite)]=juce::jlimit(-4.0f,4.0f,monoIn + fbL);
                tapeR[size_t(tapeWrite)]=juce::jlimit(-4.0f,4.0f,monoIn + fbR);
                tapeWrite=(tapeWrite+1)%int(tapeL.size());
                const float wL=shape(wetL), wR=shape(wetR);
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
                predelayL[size_t(preWrite)]=l;
                predelayR[size_t(preWrite)]=r;
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
                const float revL=tailLow[0]*1.80f+earlyL;
                const float revR=tailLow[1]*1.80f+earlyR;
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
};
