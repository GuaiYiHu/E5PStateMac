// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdint.h>
namespace profile {
constexpr uint8_t UUID[16]={0x28,0x26,0x7f,0xd5,0x3e,0x1e,0x36,0x86,0x8a,0x1c,0xe7,0x3e,0x24,0x66,0xfc,0xfb};
constexpr const char *Build="25G332";
constexpr uint32_t LcpuNumberOffset=0x44;
constexpr uint32_t PackageMinimumOffset=0x18;
constexpr uint32_t PackageMaximumOffset=0x14;
constexpr uint32_t WriterOffsetFromAnchor=0x1190;
constexpr uint32_t WriterSize=736,TimerSize=928,UrgencySize=5728,DeadlineSize=48;
constexpr uint64_t WriterFingerprint=0x137186f5201d9aaaULL;
constexpr uint64_t TimerFingerprint=0x72ef344da2ac9d10ULL;
constexpr uint64_t UrgencyFingerprint=0xed4339c2f3908768ULL;
constexpr uint64_t DeadlineFingerprint=0x8ea49c77a4a42286ULL;
} // namespace profile
