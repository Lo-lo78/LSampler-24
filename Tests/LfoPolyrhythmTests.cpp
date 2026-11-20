#include "../Source/LfoPolyrhythm.h"
#include "../Source/Parameters.h"
#include <cmath>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
int main() {
    using namespace lsampler;
    static_assert(parameterCount==174 && int(P::midi_channel)==159 && int(P::env_hold)==170);
    static_assert(grid.size()==198);
    static_assert(int(P::phase)==173);
    static_assert(int(P::lfo2_lace_mix)==169);
    for(int i=int(P::lfo1_drift_amount);i<int(P::env_hold);++i){
        assert(parameters[static_cast<size_t>(i)].initial >= parameters[static_cast<size_t>(i)].minimum);
        assert(parameters[static_cast<size_t>(i)].initial <= parameters[static_cast<size_t>(i)].maximum);
    }
    // TEST187: first and last choices remain stable; the middle Self
    // source must be reachable even after the user selects Step 5 or 20.
    const auto& source1 = parameters[size_t(P::lfo1_lace_source)];
    const auto& source2 = parameters[size_t(P::lfo2_lace_source)];
    assert(source1.minimum == 0 && source1.maximum == 2 && source1.initial == 0);
    assert(source2.minimum == 0 && source2.maximum == 2 && source2.initial == 0);
    assert(std::string(source1.labels) == "Off|LFO 1 (Self)|LFO 2");
    assert(std::string(source2.labels) == "Off|LFO 1|LFO 2 (Self)");
    for(int selectedStep: {1,5,10,15,20}) {
        for(int parameter: {int(P::lfo1_lace_source), int(P::lfo2_lace_source)}) {
            assert(laceSourceNavigationStep(parameter, double(selectedStep)) == 1.0);
            assert(laceSourceNavigationStep(parameter, double(selectedStep*10)) == 1.0);
            double current = 0;
            current = sanitise(parameters[size_t(parameter)],
                              current + laceSourceNavigationStep(parameter,double(selectedStep)));
            assert(current == 1);
            current = sanitise(parameters[size_t(parameter)], current + laceSourceNavigationStep(parameter,20.));
            assert(current == 2);
            current = sanitise(parameters[size_t(parameter)], current - laceSourceNavigationStep(parameter,20.));
            assert(current == 1);
        }
    }
    assert(laceSourceNavigationStep(int(P::lfo1_rate),10.0)==10.0);
    for(double phase: {0.,0.17,0.50,0.91}) {
        assert(driftPhase(phase,0,0.8,7)==phase);
        assert(driftPhase(phase,9,0.,7)==phase);
        assert(driftPhase(phase,10,0.8,1)==phase);
        assert(std::abs(driftPhase(phase,3,0.8,7)-driftPhase(phase,10,0.8,7))<1.e-14);
        assert(std::abs(driftHold(3,driftPhase(phase,3,.8,7),7)
                      -driftHold(10,driftPhase(phase,10,.8,7),7))<1.e-14);
    }
    assert(lace(.2,-.8,.01,4,1.)==.2);
    assert(lace(.2,-.8,.14,4,1.)==-.8);
    assert(std::abs(lace(.2,-.8,.14,4,.5)+.3)<1.e-12);
    assert(lace(.2,-.8,.14,4,0.)==.2);
    // Self-lace is bounded and reads the previous sample, not the current one.
    double prev=0;
    for(int i=0;i<5000;++i){
        const double wave=std::sin(i*0.01);
        const double current=lace(wave,prev,double(i%200)/200.,7,.85);
        assert(std::isfinite(current));assert(std::abs(current)<=1.000001);
        prev=current;
    }
    std::cout<<"PASS TEST187 LFO Self-Lace choice navigation, Drift/Lace parity\n";
}
