// SPDX-License-Identifier: BSD-2-Clause
#include "../src/NonHwpGovernor.hpp"
#include <assert.h>
#include <stdio.h>

int main() {
    const e5::GovernorConfig config{{8,29,35},8,35,4000000,false};
    auto full=e5::chooseNonHwp({29000000,32000000,29000000},config,0,20);
    assert(full.sampled && full.ratio==35 && full.requestChanged && full.busyQ8==256);
    assert(!e5::chooseNonHwp({29000000,32000000,29000000},config,0,35).requestChanged);
    auto limited=config;limited.policyMaximum=24;
    assert(e5::chooseNonHwp({29000000,32000000,29000000},limited,0,0).ratio==24);
    auto noTurbo=config;noTurbo.noTurbo=true;noTurbo.policyMaximum=29;
    assert(e5::chooseNonHwp({29000000,29000000,29000000},noTurbo,0,0).ratio==29);
    assert(!e5::chooseNonHwp({29000000,0,0},config,0,35).sampled);
    assert(!e5::chooseNonHwp({29000000,UINT64_MAX,29000000},config,0,0).sampled);
    e5::GovernorState boost={};
    e5::ioWaitFeedback(boost,4000000,true,4000000);assert(boost.ioWaitQ8==32);
    e5::ioWaitFeedback(boost,5000000,true,4000000);assert(boost.ioWaitQ8==64);
    e5::ioWaitFeedback(boost,6000000,true,4000000);assert(boost.ioWaitQ8==128);
    e5::ioWaitFeedback(boost,7000000,true,4000000);assert(boost.ioWaitQ8==256);
    e5::ioWaitFeedback(boost,8000000,true,4000000);assert(boost.ioWaitQ8==256);
    e5::ioWaitFeedback(boost,9000000,false,4000000);assert(boost.ioWaitQ8==128);
    e5::ioWaitFeedback(boost,20000000,true,4000000);assert(boost.ioWaitQ8==32);
    e5::ioWaitFeedback(boost,25000000,false,4000000);assert(boost.ioWaitQ8==0);
    e5::GovernorState state={};
    assert(!e5::updateNonHwp(state,config,{29000000,29000000,29000000},10000000,false,0,0).sampled);
    assert(!e5::updateNonHwp(state,config,{43500000,43500000,43500000},15000000,false,0,1).sampled);
    assert(state.lastUpdateNs==10000000);
    assert(!e5::updateNonHwp(state,config,{43500000,43500000,43500000},15000000,false,0,0).sampled);
    auto decision=e5::updateNonHwp(state,config,{58000000,58000000,58000000},20000000,false,0,0);
    assert(decision.sampled && decision.ratio==35 && decision.requestChanged);
    decision=e5::updateNonHwp(state,config,{87000000,87000000,87000000},30000000,false,0,0);
    assert(decision.sampled && decision.ratio==35 && !decision.requestChanged);
    assert(!e5::updateNonHwp(state,config,{10,10,10},40000000,false,0,0).sampled);
    assert(!e5::updateNonHwp(state,config,{20,20,20},35000000,false,0,0).sampled);
    puts("PASS: full load, policy cap, no-turbo, no zero-MPERF update, I/O-wait boost/decay, local CPU, 10ms gate, dedup and reset");
}
