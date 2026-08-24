// Sliding-window transmit budget. The app must call evaluate() before every
// transmission and record() after every one that actually went out.
//
// Airtime is accumulated into one-minute buckets rather than per-packet entries.
// A 1% budget at SF7 permits several hundred frames an hour, and a per-packet
// ring would either need kilobytes or silently drop history -- and dropped
// history means under-counted usage, which is the one error mode that turns a
// budget into a violation. Bucket boundaries are always resolved in the
// conservative direction: a bucket stays in the window until it has entirely
// left it.
#pragma once

#include <cstdint>

namespace lorascout {

class DutyCycleTracker {
public:
    static constexpr uint32_t kWindowMs = 3600000;  // one hour
    static constexpr uint32_t kBuckets = 60;
    static constexpr uint32_t kBucketMs = kWindowMs / kBuckets;

    struct Decision {
        bool allowed = false;
        uint32_t waitMs = 0;
        const char* reason = "";
    };

    DutyCycleTracker();

    // fraction <= 0 disables the duty limit; maxDwellMs == 0 disables the
    // per-transmission dwell cap.
    void configure(double fraction, uint32_t maxDwellMs);

    Decision evaluate(uint64_t nowMs, uint32_t airtimeMs) const;
    void record(uint64_t nowMs, uint32_t airtimeMs);

    uint32_t usedMs(uint64_t nowMs) const;
    uint32_t budgetMs() const;
    double utilization(uint64_t nowMs) const;

    // Shortest legal spacing between identical frames, which is the number the
    // UI shows as "sample every N s" -- the practical ceiling on survey density.
    uint32_t minIntervalMs(uint32_t airtimeMs) const;

    double fraction() const { return fraction_; }
    uint32_t maxDwellMs() const { return maxDwellMs_; }

    void reset();

private:
    void expire(uint64_t nowMs) const;
    static uint64_t epochOf(uint64_t nowMs) { return nowMs / kBucketMs; }

    double fraction_ = 0.0;
    uint32_t maxDwellMs_ = 0;
    // Mutable so const queries can lazily discard buckets that have aged out.
    mutable uint32_t bucketAirtimeMs_[kBuckets] = {};
    mutable uint64_t bucketEpoch_[kBuckets] = {};
    mutable bool primed_ = false;
};

}  // namespace lorascout
