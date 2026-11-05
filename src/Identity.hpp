// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "Policy.hpp"

namespace e5mac {
struct PlatformRatios {
    uint32_t operatingMinimum;
    e5::Limits linuxLimits;
};
inline PlatformRatios decodePlatform(uint64_t platform,uint64_t turbo0) {
    // Intel SDM: 55:48 = Minimum Operating Ratio, 47:40 = Maximum
    // Efficiency Ratio. Linux v6.8 core_get_min_pstate() uses the latter.
    return {static_cast<uint32_t>((platform>>48)&255),
        {static_cast<uint32_t>((platform>>40)&255),
         static_cast<uint32_t>((platform>>8)&255),static_cast<uint32_t>(turbo0&255)}};
}
inline bool stock2666Platform(const PlatformRatios &p) {
    return p.operatingMinimum==8 && e5::validLimits(p.linuxLimits) &&
        p.linuxLimits.minimum>=p.operatingMinimum && p.linuxLimits.nominal==29 &&
        p.linuxLimits.turbo==35;
}
} // namespace e5mac
