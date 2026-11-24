#include "../Source/Parameters.h"
#include <cassert>
#include <iostream>
using namespace lsampler;
int main() {
    static_assert(int(P::bp_cutoff)==175 && int(P::lfo1_bp_depth)==176 && int(P::lfo2_bp_depth)==177);
    SlotParameters p;
    assert(p[P::lfo1_bp_depth]==0 && p[P::lfo2_bp_depth]==0);
    p[P::lp_cutoff]=.7; p[P::hp_cutoff]=.3;
    p[P::lfo1_lp_depth]=3; p[P::lfo1_hp_depth]=-5;
    assert((lfoFilterDepths(p,0,true,true)==std::array<double,2>{3,-5}));
    p[P::lfo1_bp_depth]=24;
    assert((lfoFilterDepths(p,0,true,true)==std::array<double,2>{27,19}));
    assert((lfoFilterDepths(p,0,true,false)==std::array<double,2>{27,0}));
    assert((lfoFilterDepths(p,0,false,true)==std::array<double,2>{0,19}));
    assert((lfoFilterDepths(p,0,false,false)==std::array<double,2>{0,0}));
    assert((lfoFilterDepths(p,1,true,true)==std::array<double,2>{0,0}));
    p[P::lfo2_bp_depth]=-12;
    assert((lfoFilterDepths(p,1,true,true)==std::array<double,2>{-12,-12}));
    p[P::lfo1_lp_depth]=-24;p[P::lfo1_hp_depth]=-24;
    assert((lfoFilterDepths(p,0,true,true)==std::array<double,2>{0,0}));
    assert(p[P::lp_cutoff]==.7 && p[P::hp_cutoff]==.3);
    for (auto key:{P::lfo1_bp_depth,P::lfo2_bp_depth}) {
        const auto& d=parameters[size_t(key)];
        assert(d.minimum==-96 && d.maximum==96 && d.initial==0 && d.step==.1);
    }
    std::cout<<"PASS TEST191 linked LFO depth, bipolar sum, enable gating, independent LFOs, unchanged cutoff values\n";
}
