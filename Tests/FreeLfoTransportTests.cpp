#include "../Source/FreeLfoTransport.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <optional>
#include <vector>

using namespace lsampler;

int main() {
    HostPlayRestartDetector host;
    assert(!host.observe(false, 0, false, 512));
    assert(host.observe(true, 0, false, 512));
    assert(!host.observe(true, 512, false, 512));
    assert(!host.observe(true, 1024, false, 512));
    assert(!host.observe(false, 1536, false, 512));
    assert(host.observe(true, 0, false, 512));
    assert(!host.observe(true, 512, false, 512));
    // Host with no callback during Stop: a restart at an earlier position.
    assert(host.observe(true, 0, false, 512));
    assert(!host.observe(true, 512, false, 512));
    // DAW cycle/loop wrap is NOT another transport Play.
    host.reset();
    assert(host.observe(true, 8192, true, 512));
    assert(!host.observe(true, 100, true, 512));
    assert(!host.observe(true, 612, true, 512));
    // Two different Free LFOs and two polyphonic notes have unique stable seeds.
    assert(freeLfoSeed(0,0,60)==freeLfoSeed(0,0,60));
    assert(freeLfoSeed(0,0,60)!=freeLfoSeed(0,1,60));
    assert(freeLfoSeed(0,0,60)!=freeLfoSeed(1,0,60));
    assert(freeLfoSeed(0,0,60)!=freeLfoSeed(0,0,64));
    // Re-entrant Free S&H sequences are identical after the host Play reset.
    const auto sequence = [](int slot,int lfo,int note) {
        uint32_t rng = freeLfoSeed(slot,lfo,note);
        std::array<double,64> samples {};
        for(auto& sample:samples)sample=nextFreeLfoHold(rng);
        return samples;
    };
    const auto first=sequence(5,0,60);
    uint32_t unrelatedState=freeLfoSeed(2,1,72);
    for(int i=0;i<1000;++i)(void)nextFreeLfoHold(unrelatedState);
    const auto second=sequence(5,0,60);
    assert(first==second);
    assert(first!=sequence(5,1,60));
    assert(first!=sequence(6,0,60));
    assert(first!=sequence(5,0,72));
    for(auto sample:first)assert(sample>=-1.0 && sample<1.0);
    // No false restart from missing host position.
    host.reset();
    assert(host.observe(true, std::nullopt, false, 512));
    assert(!host.observe(true, std::nullopt, false, 512));
    assert(!host.observe(false, std::nullopt, false, 512));
    assert(host.observe(true, std::nullopt, false, 512));
    // A large unexpected non-looping gap is a new start/seek if Stop callbacks were omitted.
    host.reset();
    assert(host.observe(true, 100, false, 512));
    assert(!host.observe(true, 612, false, 512));
    assert(host.observe(true, 40000, false, 512));
    std::cout << "TEST170 Free LFO transport/S&H checks passed\n";
    return 0;
}
