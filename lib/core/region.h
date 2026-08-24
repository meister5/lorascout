// Regulatory limits, expressed as data the firmware enforces rather than prose
// in a README. Every transmit path in the app must clear checkChannel() and the
// duty-cycle accountant before the PA is keyed.
//
// All power limits here are stored as EIRP in dBm for a single consistent unit.
// Regions that legislate in ERP (notably EU/UK) have been converted at the
// standard +2.15 dB dipole offset, so 14 dBm ERP appears below as 16.15 EIRP.
#pragma once

#include <cstddef>
#include <cstdint>

namespace lorascout {

// Hard limits of the Cap LoRa-1262 itself. The SX1262 die is wider-band, but
// the module is specified, matched and filtered for 868-923 MHz and the shipped
// RP-SMA antenna is cut for it. Operating outside this is out of spec regardless
// of what the local regulator permits.
constexpr double kModuleFreqMinMhz = 868.0;
constexpr double kModuleFreqMaxMhz = 923.0;

// SX1262 PA range.
constexpr int kRadioMinTxDbm = -9;
constexpr int kRadioMaxTxDbm = 22;

// Gain of the antenna supplied with the cap.
constexpr double kStockAntennaGainDbi = 3.0;

enum class Region : uint8_t {
    EU868 = 0,
    UK868,
    US915,
    AU915,
    AS923,
    KR920,
    // Listed so the UI can explain *why* they are unavailable rather than
    // silently omitting them and leaving the user to guess.
    IN865,
    RU864,
    EU433,
    Count,
};

struct SubBand {
    double loMhz;
    double hiMhz;
    double maxEirpDbm;
    // Fraction of an hour the transmitter may occupy. 0 means no duty limit.
    double dutyFraction;
};

struct RegionSpec {
    Region id;
    const char* code;
    const char* name;
    double bandLoMhz;
    double bandHiMhz;
    const SubBand* subBands;
    size_t subBandCount;
    double defaultEirpDbm;
    double defaultDutyFraction;
    // Maximum continuous channel occupancy. 0 means no dwell limit.
    uint32_t maxDwellMs;
    bool requiresLbt;
    double defaultListenMhz;
    double defaultBeaconMhz;
    // Channel plan used to hop the beacon. Single-channel operation is not
    // lawful everywhere; see note.
    const double* hopChannelsMhz;
    size_t hopChannelCount;
    // True where a stationary single-channel transmitter is not lawful and the
    // beacon must hop. Where it is false, hopping is available but off by
    // default, because a rover can only follow a hopping beacon once both ends
    // have GNSS time.
    bool requiresHopping;
    bool moduleSupported;
    const char* note;
};

const RegionSpec& regionSpec(Region r);
const RegionSpec& regionAt(size_t index);
constexpr size_t regionCount() { return static_cast<size_t>(Region::Count); }
const char* regionCode(Region r);
// Returns Region::Count when the code is unknown.
Region regionFromCode(const char* code);

struct ChannelVerdict {
    bool allowed = false;
    double maxEirpDbm = 0.0;
    double dutyFraction = 0.0;
    uint32_t maxDwellMs = 0;
    bool requiresLbt = false;
    // Static string, safe to display or log. Always populated.
    const char* reason = "";
};

// Combined hardware + regulatory check for a single carrier frequency.
ChannelVerdict checkChannel(Region r, double freqMhz);

// Highest conducted power that keeps EIRP within the limit for a given antenna,
// clamped to what the SX1262 can actually produce. Rounded down: the clamp must
// never round a transmitter up past a legal ceiling.
int maxConductedDbm(double maxEirpDbm, double antennaGainDbi);

// True when the module's 868-923 MHz coverage only partially overlaps the
// region's legal band, which is the case in most of the world.
bool bandCoverageIsPartial(Region r);

// Which channel of a region's plan is active at a given moment. Both ends
// compute this from GNSS time, which is the only clock they share without ever
// having been in contact. Returns 0 when there is no plan or no time, which is
// the fallback a receiver without a time fix parks on.
size_t hopChannelIndex(Region r, uint64_t unixSeconds, uint32_t periodSeconds);
double hopChannelMhz(Region r, uint64_t unixSeconds, uint32_t periodSeconds);

// How long the current hop dwell has left, in seconds.
uint32_t hopSecondsRemaining(uint64_t unixSeconds, uint32_t periodSeconds);

}  // namespace lorascout
