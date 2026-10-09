#pragma once
#include "HostParameter.h"
#include <cstring>

namespace lsampler {
// One shared eligibility rule for the Alt+K editor and the additional VST3
// automation parameters. Structural/audio-file operations cannot be macros.
inline bool bankMacroEligible(const GridEntry& e) noexcept
{
    if (e.global >= 0 || e.loop >= 0 || e.action != Action::none || e.parameter < 0)
        return false;
    const auto p = static_cast<P>(e.parameter);
    if (!hostAutomatable(p) || p == P::low || p == P::high || p == P::root ||
        p == P::velocity_low || p == P::velocity_high ||
        p == P::choke_trigger || p == P::choke_target || p == P::choke_mode ||
        std::strcmp(e.category, "Sample Window") == 0 ||
        p == P::normalize_on || p == P::normalize_target ||
        p == P::ram_downsample || p == P::ram_fade_in || p == P::ram_fade_out ||
        p == P::ram_reverse || p == P::dc_remove)
        return false;
    return true;
}

inline Descriptor bankMacroDeltaDescriptor(int index) noexcept
{
    const auto& source = parameters[size_t(index)];
    const double limit = source.maximum - source.minimum;
    return {source.key, source.name, "Bank Macro", source.kind == Kind::continuous ? Kind::continuous : Kind::integer,
            0.0, -limit, limit, source.step, source.largeStep, source.unit, source.decimals, ""};
}
}
