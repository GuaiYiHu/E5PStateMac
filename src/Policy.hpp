// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <stdint.h>

namespace e5 {
struct Limits { uint32_t minimum, nominal, turbo; };
struct Counters { uint64_t tsc, aperf, mperf; };
struct Metrics {
    bool valid;
    bool frequencyValid;
    uint32_t busyBasisPoints; // 10000 = one logical CPU busy for the entire interval.
    uint32_t busyMHzEstimate; // APERF/MPERF * nominal ratio * assumed 100 MHz BCLK.
    uint32_t averageRatioMilli;
};

inline bool validLimits(const Limits &l) {
    return l.minimum >= 1 && l.minimum <= l.nominal &&
        l.nominal <= l.turbo && l.turbo <= 80;
}

// Bounded multiplication avoids compiler-emitted 128-bit runtime helpers in a kext.
inline bool scaled(uint64_t a, uint64_t b, uint64_t divisor, uint64_t &out) {
    if (!divisor || (b && a > UINT64_MAX / b)) return false;
    out = a * b / divisor;
    return true;
}

inline Metrics measure(const Counters &old, const Counters &now, const Limits &l) {
    Metrics m = {};
    if (!validLimits(l) || now.tsc <= old.tsc || now.aperf < old.aperf ||
        now.mperf < old.mperf) return m;
    const uint64_t dt = now.tsc - old.tsc;
    const uint64_t da = now.aperf - old.aperf;
    const uint64_t dm = now.mperf - old.mperf;
    const uint64_t nominalHz = static_cast<uint64_t>(l.nominal) * 100000000;
    // Discard very short samples, long scheduling gaps, counter resets and implausible data.
    if (dt < nominalHz / 1000 || dt > nominalHz * 5 || dm > dt + dt / 20) return m;
    uint64_t busy;
    if (!scaled(dm, 10000, dt, busy)) return m;
    m.busyBasisPoints = static_cast<uint32_t>(busy > 10000 ? 10000 : busy);
    m.valid = true;
    if (!dm) {
        if (da) m.valid = false;
        return m;
    }
    uint64_t ratio;
    if (!scaled(da, static_cast<uint64_t>(l.nominal) * 1000, dm, ratio) ||
        ratio > static_cast<uint64_t>(l.turbo) * 1050) {
        m.valid = false;
        return m;
    }
    m.averageRatioMilli = static_cast<uint32_t>(ratio);
    m.busyMHzEstimate = static_cast<uint32_t>(ratio / 10);
    m.frequencyValid = true;
    return m;
}

// E5 v3: 1AD contains ratios for 1..8 active cores, 1AE for 9..16.
inline uint32_t turboRatioForCores(uint64_t ratio0, uint64_t ratio1, unsigned cores) {
    if (!cores || cores > 16) return 0;
    return static_cast<uint32_t>((cores <= 8 ? ratio0 >> (8 * (cores - 1)) :
                                 ratio1 >> (8 * (cores - 9))) & 0xff);
}
} // namespace e5
