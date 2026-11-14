#pragma once
// LSampler-24 TEST186: deterministic phase-drift and interlace based on Biscot1.
// No allocation, RNG, or cross-sample side effects; valid for Mono and Poly.
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace lsampler {
inline double driftPhase(double phase,std::uint64_t cycles,double amount,int period) noexcept {
    if(amount<=0.0 || period<=1)return phase;
    const auto length=static_cast<std::uint64_t>(std::clamp(period,1,32));
    const double shift=std::clamp(amount,0.0,1.0)*double(cycles%length)/double(length);
    const double x=phase+shift;
    return x-std::floor(x);
}
inline double driftHold(std::uint64_t cycles,double shiftedPhase,int period) noexcept {
    const auto cell=cycles%static_cast<std::uint64_t>(std::clamp(period,1,32));
    const auto half=shiftedPhase>=0.5?1ULL:0ULL;
    std::uint64_t hash=(cell*2ULL+half+1ULL)*0x9e3779b97f4a7c15ULL;
    hash^=hash>>30;hash*=0xbf58476d1ce4e5b9ULL;
    hash^=hash>>27;hash*=0x94d049bb133111ebULL;
    hash^=hash>>31;
    return double(hash&0xffffffULL)/8388607.5-1.0;
}
inline double lace(double own,double source,double phase,int rate,double mix) noexcept {
    if(mix<=0.0)return own;
    const int slices=std::clamp(rate,1,32)*2;
    const double x=phase-std::floor(phase);
    const int slot=std::clamp(int(x*slices),0,slices-1);
    if((slot&1)==0)return own;
    return own+(source-own)*std::clamp(mix,0.0,1.0);
}
} // namespace lsampler
