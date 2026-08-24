// Session metadata and its JSON serialisation.
//
// session.json is the provenance record for a survey: which region and power
// limits were in force, what the radio was configured to, how much airtime was
// actually spent, and how the fix quality held up. A coverage map without that
// context cannot be checked by anyone else, and a transmit log without it
// cannot be audited at all.
#pragma once

#include <cstdint>
#include <string>

#include "linkstats.h"
#include "nmea.h"
#include "region.h"

namespace lorascout {

constexpr const char* kFirmwareVersion = "0.1.0";

enum class Mode : uint8_t {
    Sweep = 0,   // tier 1: geotagged noise floor
    Listen,      // tier 2: passive packet map
    Beacon,      // tier 3a: transmit a known reference
    Rover,       // tier 3b: receive a known reference and measure the link
    Count,
};

const char* modeName(Mode m);
bool modeTransmits(Mode m);

struct SessionInfo {
    char id[24] = {};
    char mode[12] = {};
    char regionCode[8] = {};
    char presetName[20] = {};
    uint16_t nodeId = 0;

    double freqMhz = 0.0;
    float bwKhz = 125.0f;
    uint8_t sf = 7;
    uint8_t cr = 5;

    // Compliance envelope actually applied, not merely intended.
    int txDbm = 0;
    double antennaGainDbi = kStockAntennaGainDbi;
    double maxEirpDbm = 0.0;
    double dutyFraction = 0.0;
    uint32_t maxDwellMs = 0;
    bool hopping = false;

    GnssTime startUtc;
    GnssTime endUtc;
    uint64_t startUptimeMs = 0;
    uint64_t endUptimeMs = 0;

    uint32_t sweepSamples = 0;
    uint32_t packetSamples = 0;
    uint32_t linkSamples = 0;
    uint32_t trackPoints = 0;
    uint32_t samplesWithoutFix = 0;

    uint32_t txCount = 0;
    uint32_t txAirtimeMs = 0;

    // Diagnostics from the sampler/writer split. A non-zero drop count means
    // the card could not keep up and the survey has holes that are the
    // firmware's fault, not the radio's -- so it must be recorded.
    uint32_t queueHighWater = 0;
    uint32_t droppedSamples = 0;

    bool payloadCaptureEnabled = false;
};

// "20260824T134502Z" when the receiver has time, "boot-0000012345" when it does
// not. Sessions must be nameable before the first fix, or an indoor cold start
// has nowhere to write.
std::string makeSessionId(const GnssTime& t, uint64_t uptimeMs);

std::string sessionDirName(const SessionInfo& s);

std::string sessionJson(const SessionInfo& s, const LinkStats& link);

}  // namespace lorascout
