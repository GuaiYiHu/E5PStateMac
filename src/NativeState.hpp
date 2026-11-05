// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include "AdapterPolicy.hpp"

namespace e5mac {
struct UrgencyBody {bool valid,routed;uint64_t normalized;};
inline UrgencyBody validateUrgencyBody(const void *memory,size_t length,uint64_t expected) {
    if (!memory || length<6) return {};
    const auto *bytes=static_cast<const uint8_t *>(memory);
    const auto observed=fingerprint(memory,length);
    if (observed==expected) return {true,false,observed};
    // CpuTscSync 1.1.2 installs a five-byte direct jump. We never route or
    // call this shared entry. Verify the ENTIRE native body after restoring
    // only the known entry bytes in the hash calculation, without writing.
    if (bytes[0]!=0xE9) return {};
    constexpr uint8_t nativePrefix[]={0x55,0x48,0x89,0xE5,0x41};
    uint64_t normalized=14695981039346656037ULL;
    for (size_t i=0;i<length;++i)
        normalized=(normalized^(i<sizeof(nativePrefix)?nativePrefix[i]:bytes[i]))*1099511628211ULL;
    return {normalized==expected,true,normalized};
}
constexpr uint32_t LogicalCpuCount=20;
constexpr size_t CpuStride=0x380,UrgencyOffset=0xCC;
inline bool nativeCpuOffset(unsigned index,size_t &offset) {
    if (index>=LogicalCpuCount) return false;
    offset=index*CpuStride+UrgencyOffset;return true;
}
} // namespace e5mac
