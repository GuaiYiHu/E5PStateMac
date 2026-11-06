// SPDX-License-Identifier: BSD-2-Clause
#include "../src/NativeState.hpp"
#include "../src/Profile.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc,char **argv) {
    assert(argc==2);
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<uint8_t> native((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    assert(native.size()==profile::UrgencySize);
    auto result=e5mac::validateUrgencyBody(native.data(),native.size(),profile::UrgencyFingerprint);
    assert(result.valid && !result.routed);
    // Regression fixture: exact entry jump independently reconstructed from
    // the observed 0.3.3 fingerprint and loaded CpuTscSync text range.
    auto shared=native;
    const uint8_t jump[]={0xE9,0x21,0xEB,0x36,0x05};
    for (unsigned i=0;i<5;++i) shared[i]=jump[i];
#ifndef E5_PROFILE_SEQUOIA_24H232
    assert(e5mac::fingerprint(shared.data(),shared.size())==0xCBE6CE30239AA3A0ULL);
#endif
    assert(e5mac::fingerprint(shared.data(),shared.size())!=profile::UrgencyFingerprint);
    result=e5mac::validateUrgencyBody(shared.data(),shared.size(),profile::UrgencyFingerprint);
    assert(result.valid && result.routed && result.normalized==profile::UrgencyFingerprint);
    for (size_t index:{size_t(5),size_t(100),shared.size()-1}) {
        auto corrupt=shared;corrupt[index]^=1;
        assert(!e5mac::validateUrgencyBody(corrupt.data(),corrupt.size(),profile::UrgencyFingerprint).valid);
    }
    shared[0]=0xFF;
    assert(!e5mac::validateUrgencyBody(shared.data(),shared.size(),profile::UrgencyFingerprint).valid);
    assert(!e5mac::validateUrgencyBody(nullptr,0,profile::UrgencyFingerprint).valid);
    size_t offset=0;
    assert(e5mac::nativeCpuOffset(0,offset) && offset==0xCC);
    assert(e5mac::nativeCpuOffset(19,offset) && offset==19*0x380+0xCC);
    assert(!e5mac::nativeCpuOffset(20,offset));
    assert(!e5mac::nativeCpuOffset(UINT32_MAX,offset));
    puts("PASS: observed CpuTscSync jump/body reconstruction, reject body corruption/unknown entry, bound native CPU-state reads");
}
