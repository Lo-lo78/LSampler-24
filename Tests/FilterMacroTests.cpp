#include "../Source/Parameters.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace lsampler;
static bool near(double a,double b) { return std::abs(a-b)<1e-12; }
static const GridEntry& entry(P parameter) {
    for (const auto& e:grid) if(e.parameter==int(parameter)&&e.loop<0&&e.global<0) return e;
    std::abort();
}
int main() {
    static_assert(int(P::phase)==173 && int(P::hp_key_follow)==174 && int(P::bp_cutoff)==175);
    SlotParameters p;
    assert(p[P::hp_key_follow]==0 && p[P::lp_cutoff]==1 && p[P::hp_cutoff]==0);
    auto set=[&](P key,double value){p.set(entry(key),0,value);};
    auto get=[&](P key){return p.get(entry(key),0);};
    set(P::lp_cutoff,.6);set(P::hp_cutoff,.3);
    assert(near(get(P::bp_cutoff),.45));
    set(P::bp_cutoff,.55);
    assert(near(get(P::lp_cutoff),.7)&&near(get(P::hp_cutoff),.4));
    const double ratio=std::pow(500.,.3);
    assert(near(std::pow(500.,get(P::lp_cutoff)-get(P::hp_cutoff)),ratio));
    set(P::bp_cutoff,1);
    assert(near(get(P::lp_cutoff),1)&&near(get(P::hp_cutoff),.7)&&near(get(P::bp_cutoff),.85));
    set(P::bp_cutoff,.84);
    assert(near(get(P::lp_cutoff),.99)&&near(get(P::hp_cutoff),.69));
    set(P::bp_cutoff,0);
    assert(near(get(P::lp_cutoff),.3)&&near(get(P::hp_cutoff),0)&&near(get(P::bp_cutoff),.15));
    set(P::lp_cutoff,.2);set(P::hp_cutoff,.8); // crossed cutoffs are valid creative settings
    set(P::bp_cutoff,.6);
    assert(near(get(P::lp_cutoff),.3)&&near(get(P::hp_cutoff),.9));
    set(P::lp_cutoff,.5);set(P::hp_cutoff,.5);set(P::bp_cutoff,.9);
    assert(near(get(P::lp_cutoff),.9)&&near(get(P::hp_cutoff),.9));
    set(P::lp_cutoff,1);set(P::hp_cutoff,0);set(P::bp_cutoff,.9);
    assert(near(get(P::lp_cutoff),1)&&near(get(P::hp_cutoff),0));
    set(P::hp_key_follow,-1);assert(get(P::hp_key_follow)==-1 && get(P::lp_key_follow)==0);
    assert(get(P::hp_on)==0&&get(P::lp_on)==0);
    std::cout<<"PASS TEST190 physical cutoff pair, logarithmic interval, limits, reverse, crossed, independent tracking\n";
}
