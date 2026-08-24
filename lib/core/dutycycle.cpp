#include "dutycycle.h"

namespace lorascout {

DutyCycleTracker::DutyCycleTracker() { reset(); }

void DutyCycleTracker::reset() {
    for (uint32_t i = 0; i < kBuckets; ++i) {
        bucketAirtimeMs_[i] = 0;
        bucketEpoch_[i] = 0;
    }
    primed_ = false;
}

void DutyCycleTracker::configure(double fraction, uint32_t maxDwellMs) {
    fraction_ = fraction > 0.0 ? fraction : 0.0;
    maxDwellMs_ = maxDwellMs;
}

void DutyCycleTracker::expire(uint64_t nowMs) const {
    const uint64_t current = epochOf(nowMs);
    for (uint32_t i = 0; i < kBuckets; ++i) {
        if (bucketAirtimeMs_[i] == 0) continue;
        // A bucket is in-window while its epoch is within the last kBuckets
        // epochs, inclusive of the current one.
        if (bucketEpoch_[i] + kBuckets <= current) {
            bucketAirtimeMs_[i] = 0;
            bucketEpoch_[i] = 0;
        }
    }
    primed_ = true;
}

uint32_t DutyCycleTracker::budgetMs() const {
    if (fraction_ <= 0.0) return kWindowMs;
    return static_cast<uint32_t>(static_cast<double>(kWindowMs) * fraction_);
}

uint32_t DutyCycleTracker::usedMs(uint64_t nowMs) const {
    expire(nowMs);
    uint32_t total = 0;
    for (uint32_t i = 0; i < kBuckets; ++i) total += bucketAirtimeMs_[i];
    return total;
}

double DutyCycleTracker::utilization(uint64_t nowMs) const {
    const uint32_t budget = budgetMs();
    if (budget == 0) return 0.0;
    return static_cast<double>(usedMs(nowMs)) / static_cast<double>(budget);
}

uint32_t DutyCycleTracker::minIntervalMs(uint32_t airtimeMs) const {
    if (fraction_ <= 0.0) return airtimeMs;
    const double interval = static_cast<double>(airtimeMs) / fraction_;
    return static_cast<uint32_t>(interval + 0.5);
}

DutyCycleTracker::Decision DutyCycleTracker::evaluate(uint64_t nowMs,
                                                      uint32_t airtimeMs) const {
    Decision d;

    if (maxDwellMs_ != 0 && airtimeMs > maxDwellMs_) {
        d.reason = "Frame exceeds this region's dwell-time limit. Use a lower "
                   "spreading factor or a shorter payload.";
        return d;
    }

    if (fraction_ <= 0.0) {
        d.allowed = true;
        d.reason = "OK";
        return d;
    }

    const uint32_t budget = budgetMs();
    if (airtimeMs > budget) {
        d.reason = "A single frame of this length exceeds the whole hourly budget.";
        return d;
    }

    expire(nowMs);
    uint32_t used = 0;
    for (uint32_t i = 0; i < kBuckets; ++i) used += bucketAirtimeMs_[i];

    if (used + airtimeMs <= budget) {
        d.allowed = true;
        d.reason = "OK";
        return d;
    }

    // Work out how long until enough of the window rolls off. Retire buckets
    // oldest-first until the frame fits, then report when that bucket leaves.
    const uint64_t current = epochOf(nowMs);
    uint32_t freed = 0;
    uint64_t releaseEpoch = current;
    bool found = false;

    for (uint64_t age = kBuckets; age > 0 && !found; --age) {
        const uint64_t targetEpoch = (current >= age - 1) ? current - (age - 1) : 0;
        for (uint32_t i = 0; i < kBuckets; ++i) {
            if (bucketAirtimeMs_[i] != 0 && bucketEpoch_[i] == targetEpoch) {
                freed += bucketAirtimeMs_[i];
                releaseEpoch = targetEpoch;
                if (used - freed + airtimeMs <= budget) found = true;
                break;
            }
        }
    }

    // The bucket leaves the window once the current epoch passes
    // releaseEpoch + kBuckets.
    const uint64_t releaseAtMs = (releaseEpoch + kBuckets) * kBucketMs;
    d.waitMs = releaseAtMs > nowMs ? static_cast<uint32_t>(releaseAtMs - nowMs) : 0;
    d.reason = "Hourly duty-cycle budget exhausted.";
    return d;
}

void DutyCycleTracker::record(uint64_t nowMs, uint32_t airtimeMs) {
    expire(nowMs);
    const uint64_t current = epochOf(nowMs);

    for (uint32_t i = 0; i < kBuckets; ++i) {
        if (bucketAirtimeMs_[i] != 0 && bucketEpoch_[i] == current) {
            bucketAirtimeMs_[i] += airtimeMs;
            return;
        }
    }
    for (uint32_t i = 0; i < kBuckets; ++i) {
        if (bucketAirtimeMs_[i] == 0) {
            bucketEpoch_[i] = current;
            bucketAirtimeMs_[i] = airtimeMs;
            return;
        }
    }
    // Every bucket occupied and none matched the current epoch: impossible while
    // kBuckets equals the window size in epochs, but if it ever happens, charge
    // the airtime to the largest bucket rather than losing it.
    uint32_t largest = 0;
    for (uint32_t i = 1; i < kBuckets; ++i) {
        if (bucketAirtimeMs_[i] > bucketAirtimeMs_[largest]) largest = i;
    }
    bucketAirtimeMs_[largest] += airtimeMs;
}

}  // namespace lorascout
