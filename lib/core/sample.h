// The records every mode produces. These are plain trivially-copyable structs
// with no pointers, because they cross a FreeRTOS queue from the sampler task
// on core 0 to the writer task on core 1.
#pragma once

#include <cstdint>

#include "geo.h"
#include "nmea.h"

namespace lorascout {

// Every sample carries the fix it was taken under. Quality travels with the
// measurement rather than in a side channel, so a reading taken on a 2D fix at
// HDOP 8 can be filtered out after the fact instead of quietly polluting a map.
struct GeoStamp {
    uint64_t uptimeMs = 0;
    GnssTime utc;
    Coord coord;
    float altitudeM = 0.0f;
    float speedKph = 0.0f;
    float courseDeg = 0.0f;
    float hdop = 99.99f;
    float meanCn0 = 0.0f;
    uint8_t satsUsed = 0;
    uint8_t satsInView = 0;
    FixType fixType = FixType::None;
    bool fixValid = false;

    static GeoStamp from(const GnssFix& fix, uint64_t uptimeMs);
    bool surveyGrade() const;
};

// Radio configuration in force when a sample was taken. Recorded per sample
// rather than per session because listen mode can rotate through presets.
struct RadioConfig {
    double freqMhz = 0.0;
    float bwKhz = 125.0f;
    uint8_t sf = 7;
    uint8_t cr = 5;
    uint8_t syncWord = 0x12;
    uint16_t preambleSymbols = 8;
    char presetName[20] = {};
};

// Tier 1: a geotagged noise-floor reading on one frequency.
struct SweepSample {
    GeoStamp geo;
    double freqMhz = 0.0;
    float rssiMin = 0.0f;
    float rssiMean = 0.0f;
    float rssiMax = 0.0f;
    uint16_t reads = 0;
    bool channelBusy = false;   // CAD detected LoRa activity during the dwell
};

// Tier 2: a packet heard from anyone.
struct PacketSample {
    GeoStamp geo;
    RadioConfig radio;
    float rssiDbm = 0.0f;
    float snrDb = 0.0f;
    float freqErrorHz = 0.0f;
    uint16_t lengthBytes = 0;
    bool crcOk = false;
    // FNV-1a over the payload. Enough to recognise a repeated frame or a
    // repeating sender without retaining anyone's traffic.
    uint32_t payloadHash = 0;
};

// Tier 3: a packet from a cooperating lorascout beacon, where the transmitter's
// own position and power are known and the link budget becomes computable.
struct LinkSample {
    PacketSample packet;
    uint16_t nodeId = 0;
    uint16_t seq = 0;
    Coord beaconCoord;
    int16_t beaconAltM = 0;
    int8_t beaconTxDbm = 0;
    bool beaconFixValid = false;
    // Derived at record time so the log is self-contained and an export never
    // has to recompute geometry from two half-trusted positions.
    double distanceM = 0.0;
    double bearingDeg = 0.0;
    double measuredPathLossDb = 0.0;
    double freeSpacePathLossDb = 0.0;
    // How much worse the real path is than free space. The obstruction number.
    double excessLossDb = 0.0;
};

// Fills in distance, bearing and the loss figures from the two positions.
void computeLinkGeometry(LinkSample& s);

}  // namespace lorascout
