// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "Policy.hpp"

namespace e5 {
// Reimplementation of the integer rules used by Linux 6.8 intel_pstate's
// non-HWP powersave path. The macOS callback/ownership adapter is separate.
struct GovernorConfig {
    Limits hardware;
    uint32_t policyMinimum, policyMaximum;
    uint64_t schedulerTickNs;
    bool noTurbo; // Effective prohibition: user no_turbo OR firmware turbo_disabled.
};
struct GovernorState {
    bool primed;
    Counters previous;
    uint64_t lastSampleNs, lastUpdateNs;
    uint32_t ioWaitQ8, currentRequest;
};
struct GovernorDecision {
    bool sampled;
    bool requestChanged;
    uint32_t ratio, busyQ8, averageRatio;
};

inline bool validConfig(const GovernorConfig &c) {
    const uint32_t maximum=c.noTurbo?c.hardware.nominal:c.hardware.turbo;
    return validLimits(c.hardware) && c.schedulerTickNs>0 &&
        c.policyMinimum>=c.hardware.minimum && c.policyMinimum<=maximum &&
        c.policyMaximum>=c.policyMinimum && c.policyMaximum<=c.hardware.turbo;
}

// These signals must eventually come from a real scheduler adapter. A timer
// callback cannot manufacture an I/O-wait event or Linux's local-CPU context.
inline void ioWaitFeedback(GovernorState &state,uint64_t nowNs,bool ioWait,uint64_t tickNs) {
    const bool longGap=nowNs-state.lastUpdateNs>tickNs;
    if (ioWait) {
        if (longGap || state.ioWaitQ8<32) state.ioWaitQ8=32;
        else state.ioWaitQ8=state.ioWaitQ8>=128?256:state.ioWaitQ8*2;
    } else if (state.ioWaitQ8) {
        state.ioWaitQ8=longGap?0:state.ioWaitQ8/2;
    }
    state.lastUpdateNs=nowNs;
}

inline GovernorDecision chooseNonHwp(const Counters &delta,const GovernorConfig &config,
                                      uint32_t ioWaitQ8,uint32_t previousRequest) {
    GovernorDecision result={};
    if (!validConfig(config) || !delta.tsc || !delta.mperf || ioWaitQ8>256) return result;
    uint64_t busy=0,averageQ14=0;
    if (!scaled(delta.mperf,256,delta.tsc,busy) ||
        !scaled(delta.aperf,16384,delta.mperf,averageQ14)) return result;
    // Treat physically inconsistent feedback as unavailable, not as a new request.
    if (busy>269 || averageQ14>static_cast<uint64_t>(config.hardware.turbo)*16384*105/
                                      (config.hardware.nominal*100)) return result;
    if (busy<ioWaitQ8) busy=ioWaitQ8;
    const uint32_t maximum=config.noTurbo?config.hardware.nominal:config.hardware.turbo;
    const uint32_t withHeadroom=maximum+(maximum>>2);
    uint32_t target=static_cast<uint32_t>((withHeadroom*busy)>>8);
    if (target<config.hardware.minimum) target=config.hardware.minimum;
    const uint32_t average=static_cast<uint32_t>(
        (config.hardware.nominal*averageQ14)>>14);
    if (average>target) target+=(average-target)>>1;
    const uint32_t cap=config.policyMaximum<maximum?config.policyMaximum:maximum;
    if (target<config.policyMinimum) target=config.policyMinimum;
    if (target>cap) target=cap;
    result.sampled=true;
    result.ratio=target;
    result.requestChanged=target!=previousRequest;
    result.busyQ8=static_cast<uint32_t>(busy);
    result.averageRatio=average;
    return result;
}

// State belongs to ONE logical CPU. A future macOS adapter must call locally
// and obtain exclusive frequency-control ownership before applying a decision.
// This function only returns decisions; there is no MSR write here.
inline GovernorDecision updateNonHwp(GovernorState &state,const GovernorConfig &config,
                                     const Counters &now,uint64_t nowNs,bool ioWait,
                                     uint32_t ownerCpu,uint32_t callingCpu) {
    GovernorDecision none={};
    if (!validConfig(config) || ownerCpu!=callingCpu ||
        nowNs<state.lastUpdateNs || nowNs<state.lastSampleNs) return none;
    ioWaitFeedback(state,nowNs,ioWait,config.schedulerTickNs);
    if (nowNs-state.lastSampleNs<10000000) return none;
    if (state.primed && (now.mperf==state.previous.mperf || now.tsc==state.previous.tsc))
        return none;
    const bool reset=state.primed && (now.tsc<state.previous.tsc ||
        now.aperf<state.previous.aperf || now.mperf<state.previous.mperf);
    const Counters delta={now.tsc-state.previous.tsc,
        now.aperf-state.previous.aperf,now.mperf-state.previous.mperf};
    const bool seeded=state.primed;
    state.previous=now;
    state.primed=true;
    state.lastSampleNs=nowNs;
    if (!seeded || reset) return none;
    const auto decision=chooseNonHwp(delta,config,state.ioWaitQ8,state.currentRequest);
    if (decision.sampled) state.currentRequest=decision.ratio;
    return decision;
}
} // namespace e5
