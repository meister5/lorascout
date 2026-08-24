#include "../support/check.h"
#include "airtime.h"
#include "dutycycle.h"

using namespace lorascout;

int main() {
    DutyCycleTracker t;
    t.configure(0.01, 0);

    CHECK_EQ(t.budgetMs(), 36000u);  // 1% of an hour

    // The headline number: an SF12 beacon frame may go out roughly every two
    // minutes under a 1% budget, an SF7 frame every five seconds. This is the
    // constraint that makes SF12 useless for a walking survey.
    LoraParams sf12;
    sf12.spreadingFactor = 12;
    LoraParams sf7;
    const uint32_t at12 = airtimeMs(sf12, 18);
    const uint32_t at7 = airtimeMs(sf7, 18);
    CHECK_EQ(t.minIntervalMs(at12), 131900u);
    CHECK_EQ(t.minIntervalMs(at7), 5200u);

    // Fresh window: a frame is allowed.
    DutyCycleTracker::Decision d = t.evaluate(0, at7);
    CHECK_TRUE(d.allowed);
    CHECK_EQ(d.waitMs, 0u);

    // Spend the entire budget inside one hour, then confirm it refuses and says
    // how long to wait rather than transmitting anyway.
    uint64_t now = 0;
    uint32_t sent = 0;
    while (t.evaluate(now, at7).allowed && sent < 5000) {
        t.record(now, at7);
        ++sent;
        now += 1000;  // one frame a second: far denser than the budget allows
        if (now > 3000000) break;
    }
    CHECK_TRUE(sent > 0);
    CHECK_TRUE(t.usedMs(now) <= t.budgetMs());
    DutyCycleTracker::Decision blocked = t.evaluate(now, at7);
    CHECK_FALSE(blocked.allowed);
    CHECK_TRUE(blocked.waitMs > 0);
    CHECK_CONTAINS(blocked.reason, "budget");

    // Budget must never be exceeded no matter how hard the caller pushes.
    CHECK_TRUE(t.utilization(now) <= 1.0);

    // Once the window has fully rolled past, the budget is available again.
    CHECK_TRUE(t.evaluate(now + DutyCycleTracker::kWindowMs + DutyCycleTracker::kBucketMs,
                          at7).allowed);

    // Dwell limits reject a frame outright: waiting does not help, so no wait
    // is offered.
    DutyCycleTracker dwell;
    dwell.configure(0.0, 400);
    DutyCycleTracker::Decision tooLong = dwell.evaluate(0, at12);
    CHECK_FALSE(tooLong.allowed);
    CHECK_EQ(tooLong.waitMs, 0u);
    CHECK_CONTAINS(tooLong.reason, "dwell");
    CHECK_TRUE(dwell.evaluate(0, at7).allowed);

    // Unlimited region: always allowed, and the minimum interval collapses to
    // the airtime itself.
    DutyCycleTracker open;
    open.configure(0.0, 0);
    CHECK_TRUE(open.evaluate(0, at12).allowed);
    CHECK_EQ(open.minIntervalMs(at7), at7);

    // A frame longer than the entire budget can never be sent.
    DutyCycleTracker tight;
    tight.configure(0.001, 0);  // 3.6 s per hour
    CHECK_EQ(tight.budgetMs(), 3600u);
    DutyCycleTracker::Decision impossible = tight.evaluate(0, 5000);
    CHECK_FALSE(impossible.allowed);
    CHECK_CONTAINS(impossible.reason, "exceeds the whole hourly budget");

    // Accounting survives a long idle gap without leaking stale airtime.
    DutyCycleTracker gap;
    gap.configure(0.01, 0);
    gap.record(1000, 20000);
    CHECK_EQ(gap.usedMs(2000), 20000u);
    CHECK_EQ(gap.usedMs(10ull * DutyCycleTracker::kWindowMs), 0u);

    return check::finish("dutycycle");
}
