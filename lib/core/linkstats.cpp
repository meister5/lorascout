#include "linkstats.h"

#include "beacon.h"

namespace lorascout {

double LinkStats::Node::lossRatio() const {
    const uint32_t expected = received + lost;
    if (expected == 0) return 0.0;
    return static_cast<double>(lost) / static_cast<double>(expected);
}

void LinkStats::reset() {
    for (size_t i = 0; i < kMaxNodes; ++i) nodes_[i] = Node{};
    nodeCount_ = 0;
}

LinkStats::Node* LinkStats::slotFor(uint16_t nodeId) {
    for (size_t i = 0; i < nodeCount_; ++i) {
        if (nodes_[i].nodeId == nodeId) return &nodes_[i];
    }
    if (nodeCount_ >= kMaxNodes) return nullptr;
    Node* n = &nodes_[nodeCount_++];
    n->nodeId = nodeId;
    n->active = true;
    return n;
}

const LinkStats::Node* LinkStats::find(uint16_t nodeId) const {
    for (size_t i = 0; i < nodeCount_; ++i) {
        if (nodes_[i].nodeId == nodeId) return &nodes_[i];
    }
    return nullptr;
}

const LinkStats::Node& LinkStats::nodeAt(size_t index) const {
    if (index >= nodeCount_) return nodes_[0];
    return nodes_[index];
}

void LinkStats::observe(uint16_t nodeId, uint16_t seq, float rssiDbm, float snrDb,
                        double distanceM, uint64_t nowMs) {
    Node* n = slotFor(nodeId);
    if (n == nullptr) return;  // more beacons than slots; ignore the newcomer

    if (!n->seqValid) {
        n->seqValid = true;
        n->lastSeq = seq;
        ++n->received;
    } else {
        const int32_t delta = seqDelta(n->lastSeq, seq);
        if (delta == 0) {
            ++n->duplicates;
        } else if (delta < 0) {
            ++n->outOfOrder;
            // The frame was probably already counted as lost when its
            // successors arrived, so give that back rather than penalising a
            // link twice for one reordering.
            if (n->lost > 0) --n->lost;
            ++n->received;
        } else if (delta > kResyncGap) {
            ++n->resyncs;
            ++n->received;
            n->lastSeq = seq;
        } else {
            n->lost += static_cast<uint32_t>(delta - 1);
            ++n->received;
            n->lastSeq = seq;
        }
    }

    n->lastRssiDbm = rssiDbm;
    n->lastSnrDb = snrDb;
    if (rssiDbm > n->bestRssiDbm) n->bestRssiDbm = rssiDbm;
    if (n->worstRssiDbm == 0.0f || rssiDbm < n->worstRssiDbm) n->worstRssiDbm = rssiDbm;
    if (distanceM > n->farthestContactM) n->farthestContactM = distanceM;
    n->lastHeardMs = nowMs;
}

uint32_t LinkStats::totalReceived() const {
    uint32_t t = 0;
    for (size_t i = 0; i < nodeCount_; ++i) t += nodes_[i].received;
    return t;
}

uint32_t LinkStats::totalLost() const {
    uint32_t t = 0;
    for (size_t i = 0; i < nodeCount_; ++i) t += nodes_[i].lost;
    return t;
}

double LinkStats::overallLossRatio() const {
    const uint32_t expected = totalReceived() + totalLost();
    if (expected == 0) return 0.0;
    return static_cast<double>(totalLost()) / static_cast<double>(expected);
}

double LinkStats::farthestContactM() const {
    double best = 0.0;
    for (size_t i = 0; i < nodeCount_; ++i) {
        if (nodes_[i].farthestContactM > best) best = nodes_[i].farthestContactM;
    }
    return best;
}

}  // namespace lorascout
