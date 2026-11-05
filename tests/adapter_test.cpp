// SPDX-License-Identifier: BSD-2-Clause
#include "../src/AdapterPolicy.hpp"
#include <assert.h>
#include <stdio.h>

int main() {
    e5mac::Gate good={2,true,true,false,false,true,false,false,false,true};
    assert(e5mac::canSample(good) && e5mac::canControl(good));
    auto g=good;g.mode=1;assert(e5mac::canSample(g) && !e5mac::canControl(g));
    g=good;g.mode=0;assert(!e5mac::canSample(g) && !e5mac::canControl(g));
    g=good;g.mode=3;assert(!e5mac::canControl(g));
    g=good;g.hooks=false;assert(!e5mac::canControl(g));
    g=good;g.service=false;assert(!e5mac::canControl(g));
    g=good;g.suspended=true;assert(!e5mac::canControl(g));
    g=good;g.fault=true;assert(!e5mac::canControl(g));
    g=good;g.xcpm=false;assert(!e5mac::canControl(g));
    g=good;g.hwp=true;assert(!e5mac::canControl(g));
    g=good;g.paused=true;assert(!e5mac::canControl(g));
    g=good;g.assertions=true;assert(e5mac::canSample(g) && !e5mac::canControl(g));
    g=good;g.configured=false;assert(!e5mac::canSample(g) && !e5mac::canControl(g));
    const e5::Limits hardware={8,29,35};
    auto caps=e5mac::combineCaps(hardware,8,35,false);
    assert(caps.valid && caps.minimum==8 && caps.maximum==35);
    caps=e5mac::combineCaps(hardware,0,0,false);
    assert(caps.valid && caps.minimum==8 && caps.maximum==35);
    caps=e5mac::combineCaps(hardware,12,24,false);
    assert(caps.valid && caps.minimum==12 && caps.maximum==24);
    assert(e5mac::boundedRatio(35,caps)==24 && e5mac::boundedRatio(8,caps)==12);
    assert(e5mac::boundedRatio(0,caps)==0);
    assert(!e5mac::combineCaps(hardware,30,24,false).valid);
    assert(!e5mac::combineCaps(hardware,8,255,false).valid);
    caps=e5mac::combineCaps(hardware,8,35,true);assert(caps.valid && caps.maximum==29);
    assert(!e5mac::combineCaps(hardware,30,35,true).valid);
    const uint64_t now=100000000,last=95000000;
    assert(e5mac::deadline(UINT64_MAX,now,last,true,true,true)==105000000);
    assert(e5mac::deadline(102000000,now,last,true,true,true)==102000000);
    assert(e5mac::deadline(UINT64_MAX,now,last,false,true,true)==UINT64_MAX);
    assert(e5mac::deadline(UINT64_MAX,now,last,true,false,true)==UINT64_MAX);
    assert(e5mac::deadline(UINT64_MAX,now,last,true,true,false)==UINT64_MAX);
    assert(e5mac::deadline(UINT64_MAX,now,80000000,true,true,true)==110000000);
    assert(e5mac::deadline(UINT64_MAX,UINT64_MAX-1,last,true,true,true)==UINT64_MAX);
    assert(e5mac::shortJumpFits(0x100000000ULL,0x100000000ULL+5+INT32_MAX));
    assert(!e5mac::shortJumpFits(0x100000000ULL,0x100000000ULL+6+INT32_MAX));
    assert(e5mac::shortJumpFits(0x100000000ULL,0x100000000ULL+5+INT32_MIN));
    const uint8_t a[]={1,2,3},b[]={1,2,4};assert(e5mac::fingerprint(a,3)!=e5mac::fingerprint(b,3));
    puts("PASS: control gates, shadow mode, Apple caps, turbo disable, stale-cap clamp, idle/local deadlines, overflow and short-route range");
}
