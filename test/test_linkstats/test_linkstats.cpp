#include "../support/check.h"
#include "linkstats.h"

using namespace lorascout;

int main() {
    LinkStats s;

    // A clean run of consecutive sequence numbers loses nothing.
    for (uint16_t seq = 1; seq <= 10; ++seq) {
        s.observe(1, seq, -90.0f, 7.5f, 100.0 * seq, seq * 1000ull);
    }
    const LinkStats::Node* n = s.find(1);
    CHECK_TRUE(n != nullptr);
    CHECK_EQ(n->received, 10u);
    CHECK_EQ(n->lost, 0u);
    CHECK_NEAR(n->lossRatio(), 0.0, 1e-9);
    CHECK_NEAR(n->farthestContactM, 1000.0, 0.01);

    // A gap is counted as loss, not silently ignored.
    s.observe(1, 15, -95.0f, 5.0f, 1500.0, 15000);
    n = s.find(1);
    CHECK_EQ(n->lost, 4u);
    CHECK_EQ(n->received, 11u);
    CHECK_NEAR(n->lossRatio(), 4.0 / 15.0, 1e-9);

    // A repeat is a duplicate, not a new packet.
    s.observe(1, 15, -95.0f, 5.0f, 1500.0, 15100);
    n = s.find(1);
    CHECK_EQ(n->duplicates, 1u);
    CHECK_EQ(n->received, 11u);

    // A late arrival is credited back rather than penalising the link twice.
    const uint32_t lostBefore = n->lost;
    s.observe(1, 13, -97.0f, 3.0f, 1300.0, 15200);
    n = s.find(1);
    CHECK_EQ(n->outOfOrder, 1u);
    CHECK_EQ(n->lost, lostBefore - 1);
    CHECK_EQ(n->received, 12u);
    // ...and it must not rewind the sequence, or the next frame looks like a
    // huge gap.
    CHECK_EQ(n->lastSeq, 15);

    // RSSI extremes are tracked in both directions.
    CHECK_NEAR(n->bestRssiDbm, -90.0, 0.01);
    CHECK_NEAR(n->worstRssiDbm, -97.0, 0.01);

    // Wraparound past 65535 is one step, not sixty-five thousand losses.
    LinkStats w;
    w.observe(2, 65534, -90.0f, 5.0f, 0.0, 0);
    w.observe(2, 65535, -90.0f, 5.0f, 0.0, 1000);
    w.observe(2, 0, -90.0f, 5.0f, 0.0, 2000);
    w.observe(2, 1, -90.0f, 5.0f, 0.0, 3000);
    const LinkStats::Node* wn = w.find(2);
    CHECK_EQ(wn->received, 4u);
    CHECK_EQ(wn->lost, 0u);

    // A beacon that reboots resets its counter; that is a resync, not a loss
    // event large enough to destroy the session's statistics.
    LinkStats r;
    for (uint16_t seq = 1; seq <= 5; ++seq) r.observe(3, seq, -90.0f, 5.0f, 0.0, seq);
    r.observe(3, 4000, -90.0f, 5.0f, 0.0, 6000);
    const LinkStats::Node* rn = r.find(3);
    CHECK_EQ(rn->resyncs, 1u);
    CHECK_EQ(rn->lost, 0u);
    CHECK_EQ(rn->received, 6u);

    // Several beacons are tracked independently.
    LinkStats m;
    m.observe(10, 1, -80.0f, 9.0f, 50.0, 0);
    m.observe(20, 1, -110.0f, -5.0f, 5000.0, 0);
    m.observe(10, 3, -80.0f, 9.0f, 60.0, 1000);
    CHECK_EQ(m.nodeCount(), 2u);
    CHECK_EQ(m.find(10)->lost, 1u);
    CHECK_EQ(m.find(20)->lost, 0u);
    CHECK_EQ(m.totalReceived(), 3u);
    CHECK_EQ(m.totalLost(), 1u);
    CHECK_NEAR(m.overallLossRatio(), 0.25, 1e-9);
    CHECK_NEAR(m.farthestContactM(), 5000.0, 0.01);
    CHECK_TRUE(m.find(99) == nullptr);

    // Exceeding the node table must not corrupt the existing entries.
    LinkStats overflow;
    for (uint16_t id = 1; id <= LinkStats::kMaxNodes + 4; ++id) {
        overflow.observe(id, 1, -90.0f, 5.0f, 0.0, 0);
    }
    CHECK_EQ(overflow.nodeCount(), LinkStats::kMaxNodes);
    CHECK_TRUE(overflow.find(1) != nullptr);

    overflow.reset();
    CHECK_EQ(overflow.nodeCount(), 0u);
    CHECK_NEAR(overflow.overallLossRatio(), 0.0, 1e-9);

    return check::finish("linkstats");
}
