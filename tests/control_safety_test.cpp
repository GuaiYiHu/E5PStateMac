// SPDX-License-Identifier: BSD-2-Clause
#include "../src/ControlSafety.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using namespace e5mac;
    const Gate running={2,true,true,false,false,true,false,false,false,true};
    const uint64_t boot=1000000000,after=boot+LegacyWarmupNs;
    static_assert(warmupDelayNs(false)==0);
    static_assert(warmupDelayNs(true)==15000000000ULL);
    Readiness states[ExpectedCpus]={};uint32_t mask=0;
    for (unsigned tick=1;tick<=RequiredGoodSamples;++tick) {
        for (unsigned cpu=0;cpu<ExpectedCpus;++cpu) {
            // Reproduce the observed native class=0 on busy cores. The gate
            // uses local context and global PM health, not this opaque class.
            const unsigned nativeClass=0;(void)nativeClass;
            assert(sampleLocal(running,cpu));
            if (noteSample(states[cpu],boot+tick*40000000,true)) mask|=1U<<cpu;
        }
        if (tick<RequiredGoodSamples) assert(!warmupReady(after,boot,mask));
    }
    const uint64_t samplesComplete=boot+RequiredGoodSamples*40000000;
    assert(mask==AllCpusMask && warmupReady(samplesComplete,boot,mask));
    assert(samplesComplete-boot<LegacyWarmupNs);
    assert(!warmupReady(samplesComplete,boot,mask,warmupDelayNs(true)));
    assert(!warmupReady(after-1,boot,mask,warmupDelayNs(true)));
    assert(warmupReady(after,boot,mask,warmupDelayNs(true)));
    const uint64_t delays[]={warmupDelayNs(false),warmupDelayNs(true)};
    for (uint64_t delay:delays) {
        assert(!warmupReady(after,0,mask,delay));
        assert(!warmupReady(boot-1,boot,mask,delay));
        assert(!warmupReady(after,boot,mask&~(1U<<19),delay));
        assert(!warmupReady(after,boot,mask|(1U<<20),delay));
    }
    assert(!sampleLocal(running,20));
    auto broken=running;broken.suspended=true;assert(!sampleLocal(broken,0));
    broken=running;broken.fault=true;assert(!sampleLocal(broken,0));
    Readiness r={};
    for (unsigned i=1;i<8;++i) assert(!noteSample(r,i*40000000,true));
    assert(!noteSample(r,320000000,false) && r.good==0);
    assert(!noteSample(r,360000000,true) && r.good==1);
    assert(!noteSample(r,600000000,true) && r.good==1); // gap resets the streak
    assert(!noteSample(r,599000000,true) && r.good==1); // clock reversal
    const Caps stock={true,12,35};
    const uint64_t now=20000000000,sample=now-40000000;
    assert(freshDecision(now,sample,true,false));
    assert(!freshDecision(now,now+1,true,false));
    assert(!freshDecision(now,now-FreshDecisionNs-1,true,false));
    assert(!freshDecision(now,sample,false,false));
    assert(!freshDecision(now,sample,true,true));
    assert(selectRequest(20,35,stock,true,true)==35);
    assert(selectRequest(20,35,stock,false,true)==20);
    assert(selectRequest(20,35,stock,true,false)==20);
    assert(selectRequest(20,0,stock,true,true)==20);
    assert(selectRequest(20,80,stock,true,true)==35);
    const Caps noTurbo={true,12,29};
    assert(selectRequest(20,35,noTurbo,true,true)==29);
    // Linux policy continues to lower its request when load leaves. It is not
    // a constant turbo override, including when the hardware was at 3.2 GHz.
    const e5::GovernorConfig cfg={{12,29,35},12,35,4000000,false};
    const auto full=e5::chooseNonHwp({29000000,20000000,29000000},cfg,0,20);
    assert(full.sampled && full.ratio==35);
    const auto quiet=e5::chooseNonHwp({29000000,32000,29000},cfg,0,35);
    assert(quiet.sampled && quiet.ratio>=12 && quiet.ratio<35);
    puts("PASS: default self-test-only startup, opt-in 15s delay, 20 CPU qualification, invalid/gapped feedback, stale/thermal pass-through, stock/turbo caps and full-load-to-idle scaling");
}
