#include "beacon.h"

#include <cstring>

namespace lorascout {
namespace {

void putU16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

uint16_t getU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

void putI32(uint8_t* p, int32_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    p[0] = static_cast<uint8_t>(u & 0xFF);
    p[1] = static_cast<uint8_t>((u >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((u >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((u >> 24) & 0xFF);
}

int32_t getI32(const uint8_t* p) {
    const uint32_t u = static_cast<uint32_t>(p[0]) |
                       (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) |
                       (static_cast<uint32_t>(p[3]) << 24);
    return static_cast<int32_t>(u);
}

}  // namespace

size_t encodeBeacon(const BeaconFrame& f, uint8_t* out, size_t outLen) {
    if (out == nullptr || outLen < kBeaconFrameSize) return 0;

    out[0] = kBeaconMagic0;
    out[1] = kBeaconMagic1;
    out[2] = f.version;
    putU16(out + 3, f.nodeId);
    putU16(out + 5, f.seq);
    putI32(out + 7, f.coord.lat_e7);
    putI32(out + 11, f.coord.lon_e7);
    putU16(out + 15, static_cast<uint16_t>(f.altitudeM));
    out[17] = static_cast<uint8_t>(f.txDbm);
    out[18] = f.flags;
    return kBeaconFrameSize;
}

bool decodeBeacon(const uint8_t* in, size_t len, BeaconFrame* out) {
    if (in == nullptr || out == nullptr) return false;
    if (len < kBeaconFrameSize) return false;
    if (in[0] != kBeaconMagic0 || in[1] != kBeaconMagic1) return false;
    if (in[2] != kBeaconVersion) return false;

    BeaconFrame f;
    f.version = in[2];
    f.nodeId = getU16(in + 3);
    f.seq = getU16(in + 5);
    f.coord.lat_e7 = getI32(in + 7);
    f.coord.lon_e7 = getI32(in + 11);
    f.altitudeM = static_cast<int16_t>(getU16(in + 15));
    f.txDbm = static_cast<int8_t>(in[17]);
    f.flags = in[18];

    // A frame carrying an impossible coordinate is corrupt, whatever the PHY
    // CRC said. Better to drop it than to plot it.
    if (f.coord.lat_e7 > 900000000 || f.coord.lat_e7 < -900000000) return false;
    if (f.coord.lon_e7 > 1800000000 || f.coord.lon_e7 < -1800000000) return false;

    *out = f;
    return true;
}

uint32_t payloadHash(const uint8_t* data, size_t len) {
    uint32_t h = 2166136261u;
    if (data == nullptr) return h;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

int32_t seqDelta(uint16_t previous, uint16_t current) {
    return static_cast<int16_t>(static_cast<uint16_t>(current - previous));
}

}  // namespace lorascout
