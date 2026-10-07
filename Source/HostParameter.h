#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include <atomic>
#include <cstring>
namespace lsampler {
// One canonical real value. Host callbacks only publish atomics; no state lock,
// sample access, message posting, host notification, or allocation here.
class HostParameter final : public juce::AudioProcessorParameterWithID {
public:
    HostParameter(const juce::String& id, const juce::String& name, Descriptor descriptor,
                  std::atomic<uint64_t>& generation, std::atomic<uint64_t>& ui)
        : AudioProcessorParameterWithID(juce::ParameterID{id, 1}, name,
              juce::AudioProcessorParameterWithIDAttributes{}.withLabel(descriptor.unit)),
          d(descriptor), real(descriptor.initial), revision(generation), uiRevision(ui) {
        labels = juce::StringArray::fromTokens(d.labels, "|", "");
    }
    double load() const noexcept { return real.load(std::memory_order_acquire); }
    double decode(float v) const noexcept {
        return sanitise(d, d.minimum + (d.maximum-d.minimum)*std::clamp(double(v),0.0,1.0));
    }
    float encode(double v) const noexcept { return float((sanitise(d,v)-d.minimum)/(d.maximum-d.minimum)); }
    void storeReal(double v) noexcept {
        v=sanitise(d,v);
        if(syncParameter && syncParameter->load()!=0)
            v=std::clamp(std::round(v*8)/8,.125,512.0);
        if(real.exchange(v,std::memory_order_acq_rel)!=v) {
            revision.fetch_add(1,std::memory_order_release);
            uiRevision.fetch_add(1,std::memory_order_release);
            if(syncedRate && v!=0)syncedRate->storeReal(syncedRate->load());
        }
    }
    float getValue() const override { return encode(load()); }
    void setValue(float v) override { storeReal(decode(v)); }
    float getDefaultValue() const override { return encode(d.initial); }
    bool isDiscrete() const override { return d.kind!=Kind::continuous; }
    bool isBoolean() const override { return std::strcmp(d.labels,"Off|On")==0; }
    int getNumSteps() const override { return isDiscrete()?int(d.maximum-d.minimum)+1:juce::AudioProcessor::getDefaultNumParameterSteps(); }
    juce::String getText(float v,int maximumLength) const override {
        const double x=decode(v);
        auto text=labels.isEmpty()?juce::String(x,d.decimals):labels[juce::jlimit(0,labels.size()-1,int(x-d.minimum))];
        return maximumLength>0?text.substring(0,maximumLength):text;
    }
    float getValueForText(const juce::String& text) const override {
        const int i=labels.indexOf(text.trim(),true);
        return encode(i>=0?d.minimum+i:text.getDoubleValue());
    }
    HostParameter* syncParameter = nullptr;
    HostParameter* syncedRate = nullptr;
    std::atomic<bool> notificationPending {false};
    const Descriptor d;
private:
    std::atomic<double> real;
    std::atomic<uint64_t>& revision;
    std::atomic<uint64_t>& uiRevision;
    juce::StringArray labels;
};
inline bool hostAutomatable(P p) noexcept {
    // Thresholds scan potentially entire recordings and are editor operations.
    return p!=P::start_threshold && p!=P::end_threshold && p!=P::end_preview_length;
}
}
