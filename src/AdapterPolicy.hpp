// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "NonHwpGovernor.hpp"
#include <stddef.h>

namespace e5mac {
constexpr uint64_t IntervalNs=10000000;
struct Gate {
    uint32_t mode;
    bool hooks,service,suspended,fault,xcpm,hwp,paused,assertions,configured;
};
inline bool canSample(const Gate &g) {
    return (g.mode==1 || g.mode==2) && g.hooks && g.service && !g.suspended && !g.fault &&
        g.xcpm && !g.hwp && !g.paused && g.configured;
}
inline bool canControl(const Gate &g) {return g.mode==2 && canSample(g) && !g.assertions;}
struct Caps { bool valid; uint32_t minimum,maximum; };
inline Caps combineCaps(const e5::Limits &hardware,uint32_t appleMin,uint32_t appleMax,bool noTurbo) {
    Caps result={};
    if (!e5::validLimits(hardware) || appleMin>hardware.turbo || appleMax>hardware.turbo) return result;
    const uint32_t top=noTurbo?hardware.nominal:hardware.turbo;
    const uint32_t lo=appleMin>hardware.minimum?appleMin:hardware.minimum;
    const uint32_t hi=appleMax && appleMax<top?appleMax:top;
    if (lo>hi) return result;
    return {true,lo,hi};
}
inline uint32_t boundedRatio(uint32_t target,const Caps &caps) {
    if (!caps.valid || !target) return 0;
    if (target<caps.minimum) return caps.minimum;
    return target>caps.maximum?caps.maximum:target;
}
inline uint64_t deadline(uint64_t apple,uint64_t now,uint64_t lastSample,
                         bool control,bool active,bool local) {
    if (!control || !active || !local) return apple;
    if (now>UINT64_MAX-IntervalNs) return apple;
    const uint64_t next=lastSample && lastSample<=now && now-lastSample<IntervalNs?
        lastSample+IntervalNs:now+IntervalNs;
    return apple && apple<next?apple:next;
}
inline bool shortJumpFits(uint64_t from,uint64_t to) {
    const int64_t distance=static_cast<int64_t>(to-(from+5));
    return distance>=INT32_MIN && distance<=INT32_MAX;
}
inline uint64_t fingerprint(const void *memory,size_t length) {
    const auto *bytes=static_cast<const uint8_t *>(memory);
    uint64_t value=14695981039346656037ULL;
    for (size_t i=0;i<length;++i) value=(value^bytes[i])*1099511628211ULL;
    return value;
}
} // namespace e5mac
