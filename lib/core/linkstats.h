// Per-node packet accounting for tier 3.
//
// Passive listening can only ever tell you what you heard. Sequence numbers
// from a cooperating beacon are what turn that into a loss rate, which is the
// difference between "coverage looks fine here" and "coverage is 40% here".
#pragma once

#include <cstddef>
#include <cstdint>

namespace lorascout {

class LinkStats {
public:
    static constexpr size_t kMaxNodes = 8;
    // A jump larger than this is read as the beacon having restarted rather
    // than as thousands of lost frames, which would otherwise wreck the
    // statistics for the rest of the session.
    static constexpr int32_t kResyncGap = 512;

    struct Node {
        uint16_t nodeId = 0;
        bool active = false;
        uint32_t received = 0;
        uint32_t lost = 0;
        uint32_t duplicates = 0;
        uint32_t outOfOrder = 0;
        uint32_t resyncs = 0;
        uint16_t lastSeq = 0;
        bool seqValid = false;
        float lastRssiDbm = 0.0f;
        float lastSnrDb = 0.0f;
        float bestRssiDbm = -200.0f;
        float worstRssiDbm = 0.0f;
        double farthestContactM = 0.0;
        uint64_t lastHeardMs = 0;

        double lossRatio() const;
    };

    void observe(uint16_t nodeId, uint16_t seq, float rssiDbm, float snrDb,
                 double distanceM, uint64_t nowMs);

    const Node* find(uint16_t nodeId) const;
    size_t nodeCount() const { return nodeCount_; }
    const Node& nodeAt(size_t index) const;

    uint32_t totalReceived() const;
    uint32_t totalLost() const;
    double overallLossRatio() const;
    double farthestContactM() const;

    void reset();

private:
    Node* slotFor(uint16_t nodeId);

    Node nodes_[kMaxNodes];
    size_t nodeCount_ = 0;
};

}  // namespace lorascout
