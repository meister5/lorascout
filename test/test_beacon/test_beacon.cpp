#include "../support/check.h"
#include "beacon.h"
#include "preset.h"

#include <cstring>

using namespace lorascout;

int main() {
    BeaconFrame f;
    f.nodeId = 0xBEEF;
    f.seq = 1234;
    f.coord = Coord::fromDegrees(51.5004, -0.1246);
    f.altitudeM = -37;          // below sea level, to prove the sign survives
    f.txDbm = 13;
    f.flags = kBeaconFlagFix3D;

    uint8_t buf[32] = {};
    CHECK_EQ(encodeBeacon(f, buf, sizeof(buf)), kBeaconFrameSize);
    CHECK_EQ(buf[0], 'L');
    CHECK_EQ(buf[1], 'S');
    CHECK_EQ(buf[2], kBeaconVersion);

    BeaconFrame g;
    CHECK_TRUE(decodeBeacon(buf, kBeaconFrameSize, &g));
    CHECK_EQ(g.nodeId, 0xBEEF);
    CHECK_EQ(g.seq, 1234);
    CHECK_EQ(g.coord.lat_e7, f.coord.lat_e7);
    CHECK_EQ(g.coord.lon_e7, f.coord.lon_e7);
    CHECK_EQ(g.altitudeM, -37);
    CHECK_EQ(g.txDbm, 13);
    CHECK_TRUE(g.fix3D());
    CHECK_FALSE(g.positionStale());

    // Negative transmit power (the SX1262 goes to -9 dBm) must round-trip.
    f.txDbm = -9;
    f.coord = Coord::fromDegrees(-33.8688, 151.2093);
    encodeBeacon(f, buf, sizeof(buf));
    CHECK_TRUE(decodeBeacon(buf, kBeaconFrameSize, &g));
    CHECK_EQ(g.txDbm, -9);
    CHECK_NEAR(g.coord.latDeg(), -33.8688, 1e-7);
    CHECK_NEAR(g.coord.lonDeg(), 151.2093, 1e-7);

    // A buffer that cannot hold the frame is refused rather than truncated.
    uint8_t small[8] = {};
    CHECK_EQ(encodeBeacon(f, small, sizeof(small)), 0u);
    CHECK_EQ(encodeBeacon(f, nullptr, 64), 0u);

    // Anything that is not ours is rejected. A foreign packet landing on our
    // sync word must never be decoded into a fictional position.
    CHECK_FALSE(decodeBeacon(buf, kBeaconFrameSize - 1, &g));
    CHECK_FALSE(decodeBeacon(nullptr, kBeaconFrameSize, &g));
    CHECK_FALSE(decodeBeacon(buf, kBeaconFrameSize, nullptr));

    uint8_t bad[kBeaconFrameSize] = {};
    std::memcpy(bad, buf, kBeaconFrameSize);
    bad[0] = 'X';
    CHECK_FALSE(decodeBeacon(bad, sizeof(bad), &g));

    std::memcpy(bad, buf, kBeaconFrameSize);
    bad[2] = 99;  // future version
    CHECK_FALSE(decodeBeacon(bad, sizeof(bad), &g));

    // Out-of-range coordinates are corrupt whatever the PHY CRC said.
    std::memcpy(bad, buf, kBeaconFrameSize);
    bad[7] = 0xFF; bad[8] = 0xFF; bad[9] = 0xFF; bad[10] = 0x7F;  // lat_e7 = INT32_MAX
    CHECK_FALSE(decodeBeacon(bad, sizeof(bad), &g));

    // Sequence arithmetic must survive the 16-bit wrap, or every beacon would
    // appear to lose 65000 packets once an hour.
    CHECK_EQ(seqDelta(10, 11), 1);
    CHECK_EQ(seqDelta(65535, 0), 1);
    CHECK_EQ(seqDelta(65530, 4), 10);
    CHECK_EQ(seqDelta(0, 65535), -1);
    CHECK_EQ(seqDelta(5, 5), 0);

    // Payload hashing is deterministic and separates different payloads.
    const uint8_t a[] = {1, 2, 3, 4};
    const uint8_t b[] = {1, 2, 3, 5};
    CHECK_EQ(payloadHash(a, sizeof(a)), payloadHash(a, sizeof(a)));
    CHECK_TRUE(payloadHash(a, sizeof(a)) != payloadHash(b, sizeof(b)));
    CHECK_EQ(payloadHash(nullptr, 0), payloadHash(a, 0));

    // The beacon must not be able to speak any other network's language.
    CHECK_EQ(beaconPreset().syncWord, kLorascoutSyncWord);
    CHECK_TRUE(kLorascoutSyncWord != 0x34);  // LoRaWAN public
    CHECK_TRUE(kLorascoutSyncWord != 0x12);  // LoRaWAN private
    CHECK_TRUE(kLorascoutSyncWord != 0x2B);  // Meshtastic

    // Transmit is permitted on lorascout presets only; everything we listen to
    // is receive-only by construction.
    size_t transmittable = 0;
    for (size_t i = 0; i < presetCount(); ++i) {
        const RadioPreset& p = presetAt(i);
        if (p.transmitAllowed) {
            ++transmittable;
            CHECK_EQ(p.syncWord, kLorascoutSyncWord);
        }
    }
    CHECK_EQ(transmittable, 2u);

    // Every preset must resolve to a legal frequency in every supported region.
    for (size_t i = 0; i < presetCount(); ++i) {
        for (size_t r = 0; r < regionCount(); ++r) {
            const RegionSpec& spec = regionAt(r);
            if (!spec.moduleSupported) continue;
            const double f2 = presetFrequency(presetAt(i), spec.id);
            CHECK_TRUE(checkChannel(spec.id, f2).allowed);
        }
    }

    return check::finish("beacon");
}
