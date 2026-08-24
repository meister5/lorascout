// The lorascout beacon frame: 19 bytes, little-endian, no padding.
//
// Kept small because airtime is the scarce resource. At SF7/BW125 this frame
// occupies the channel for ~52 ms, which under a 1% duty cycle allows one
// roughly every five seconds -- dense enough to survey at walking pace. The
// same frame at SF12 takes 1.3 s and drops that to one every two minutes.
#pragma once

#include <cstddef>
#include <cstdint>

#include "geo.h"

namespace lorascout {

constexpr uint8_t kBeaconMagic0 = 'L';
constexpr uint8_t kBeaconMagic1 = 'S';
constexpr uint8_t kBeaconVersion = 1;
constexpr size_t kBeaconFrameSize = 19;

// Flag bits carried in the frame.
constexpr uint8_t kBeaconFlagFix3D = 0x01;
constexpr uint8_t kBeaconFlagPositionStale = 0x02;
constexpr uint8_t kBeaconFlagHopping = 0x04;

struct BeaconFrame {
    uint8_t version = kBeaconVersion;
    uint16_t nodeId = 0;
    uint16_t seq = 0;
    Coord coord;
    int16_t altitudeM = 0;
    int8_t txDbm = 0;
    uint8_t flags = 0;

    bool fix3D() const { return (flags & kBeaconFlagFix3D) != 0; }
    bool positionStale() const { return (flags & kBeaconFlagPositionStale) != 0; }
};

// Returns the number of bytes written, or 0 if the buffer is too small.
size_t encodeBeacon(const BeaconFrame& f, uint8_t* out, size_t outLen);

// Returns false for anything that is not a well-formed frame of a version we
// understand. A foreign packet that happens to land on our sync word must be
// rejected here rather than decoded into a fictional position.
bool decodeBeacon(const uint8_t* in, size_t len, BeaconFrame* out);

// FNV-1a. Used to fingerprint payloads we deliberately do not retain.
uint32_t payloadHash(const uint8_t* data, size_t len);

// Distance the sequence advanced, accounting for 16-bit wraparound. Negative
// means the frame arrived out of order.
int32_t seqDelta(uint16_t previous, uint16_t current);

}  // namespace lorascout
