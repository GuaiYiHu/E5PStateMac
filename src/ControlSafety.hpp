// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "AdapterPolicy.hpp"

namespace e5mac {
constexpr uint64_t LegacyWarmupNs=15000000000ULL;
constexpr uint64_t warmupDelayNs(bool wait15) {return wait15?LegacyWarmupNs:0;}
constexpr uint64_t FreshDecisionNs=100000000;
constexpr uint64_t MaximumSampleGapNs=250000000;
constexpr uint32_t RequiredGoodSamples=8;
constexpr uint32_t ExpectedCpus=20;
constexpr uint32_t AllCpusMask=(1U<<ExpectedCpus)-1;
struct Readiness {uint32_t good;uint64_t lastGoodNs;};
inline bool noteSample(Readiness &state,uint64_t now,bool valid) {
    if (!valid) {state={};return false;}
    if (!state.lastGoodNs || now<=state.lastGoodNs || now-state.lastGoodNs>FreshDecisionNs)
        state.good=0;
    if (state.good<RequiredGoodSamples) ++state.good;
    state.lastGoodNs=now;
    return state.good>=RequiredGoodSamples;
}
inline bool warmupReady(uint64_t now,uint64_t start,uint32_t mask,uint64_t delay=0) {
    return start && now>=start && now-start>=delay && mask==AllCpusMask;
}
inline bool freshDecision(uint64_t now,uint64_t sample,bool valid,bool thermal) {
    return valid && !thermal && sample && now>=sample && now-sample<=FreshDecisionNs;
}
// Opaque native XCPM class is diagnostic input only. In particular, a zero
// class is not evidence that APERF/MPERF sampling should be suppressed.
inline bool sampleLocal(const Gate &g,unsigned cpu) {return cpu<ExpectedCpus && canSample(g);}
inline uint32_t selectRequest(uint32_t native,uint32_t policy,const Caps &caps,
                              bool armed,bool fresh) {
    const auto target=boundedRatio(policy,caps);
    return armed && fresh && target?target:native;
}
} // namespace e5mac
