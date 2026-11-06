// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdint.h>
namespace profile {
constexpr int KernelMajor=24,KernelMinor=6;
constexpr const char *Target="sequoia-24H232";
constexpr const char *BootKCSHA256="ddd9016e225a61ebfd4f04b02a00937262f26690c57a928613e08506978c81be";
constexpr uint8_t UUID[16]={0x74,0x79,0x34,0xe9,0xfd,0x01,0x36,0xab,0x8e,0x97,0x06,0xa5,0x78,0xab,0x22,0xf9};
constexpr const char *Build="24H232";
constexpr uint32_t LcpuNumberOffset=0x44;
constexpr uint32_t PackageMinimumOffset=0x18;
constexpr uint32_t PackageMaximumOffset=0x14;
constexpr uint32_t WriterOffsetFromAnchor=0x1140;
constexpr uint32_t WriterSize=736,TimerSize=928,UrgencySize=5696,DeadlineSize=48;
constexpr uint64_t WriterFingerprint=0xf1f91f7a5eeed25cULL;
constexpr uint64_t TimerFingerprint=0x14dcf81363c3b3a8ULL;
constexpr uint64_t UrgencyFingerprint=0x50b5446de1e52e1fULL;
constexpr uint64_t DeadlineFingerprint=0x554f3d288399ef6fULL;
} // namespace profile
