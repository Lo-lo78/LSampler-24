#include "../Source/LfoPolyrhythm.h"
#include "../Source/Parameters.h"
#include <cmath>
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    using namespace lsampler;
    static_assert(parameterCount==170 && int(P::midi_channel)==159);
    static_assert(grid.size()==194);
    static_assert(int(P::lfo2_lace_mix)==169);
    for(int i=int(P::lfo1_drift_amount);i<parameterCount;++i){
        assert(parameters[static_cast<size_t>(i)].initial >= parameters[static_cast<size_t>(i)].minimum);
        assert(parameters[static_cast<size_t>(i)].initial <= parameters[static_cast<size_t>(i)].maximum);
    }
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
    std::cout<<"PASS TEST186 LFO Drift/Lace cycle parity and Self-Lace\n";
}
