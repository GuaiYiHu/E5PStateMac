// SPDX-License-Identifier: BSD-2-Clause
#include "../src/Identity.hpp"
#include <cassert>
#include <cstdio>

int main() {
    // Independent Intel-defined fields: operating minimum=8, maximum
    // efficiency=12, nominal=29. They are intentionally NOT equal.
    constexpr uint64_t platform=0x00080c0000001d00ULL;
    const auto decoded=e5mac::decodePlatform(platform,35);
    assert(decoded.operatingMinimum==8);
    assert(decoded.linuxLimits.minimum==12);
    assert(decoded.linuxLimits.nominal==29 && decoded.linuxLimits.turbo==35);
    assert(e5mac::stock2666Platform(decoded));
    // The old 0.3.1 predicate rejects this valid platform.
    assert(decoded.linuxLimits.minimum!=8);
    auto bad=decoded;bad.operatingMinimum=0;assert(!e5mac::stock2666Platform(bad));
    bad=decoded;bad.linuxLimits.minimum=0;assert(!e5mac::stock2666Platform(bad));
    bad=decoded;bad.linuxLimits.minimum=7;assert(!e5mac::stock2666Platform(bad));
    bad=decoded;bad.linuxLimits.minimum=30;assert(!e5mac::stock2666Platform(bad));
    bad=decoded;bad.linuxLimits.nominal=28;assert(!e5mac::stock2666Platform(bad));
    bad=decoded;bad.linuxLimits.turbo=36;assert(!e5mac::stock2666Platform(bad));
    assert(e5::turboRatioForCores(35,0x2020,10)==32);
    puts("PASS: distinct operating/efficiency ratios, Linux minimum selection, invalid limit rejection and 10-core turbo decoding");
}
