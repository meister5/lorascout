#include "region.h"

#include <cmath>
#include <cstring>

namespace lorascout {
namespace {

// ERP -> EIRP offset for a half-wave dipole reference.
constexpr double kErpToEirp = 2.15;
constexpr double k14dBmErp = 14.0 + kErpToEirp;   // 16.15
constexpr double k27dBmErp = 27.0 + kErpToEirp;   // 29.15

// EN 300 220 sub-bands. Only the portion at or above 868 MHz is reachable with
// this module; the 863-868 segment is listed so checkChannel can say so.
constexpr SubBand kEu868SubBands[] = {
    {863.0,   868.0,   k14dBmErp, 0.01},
    {868.0,   868.6,   k14dBmErp, 0.01},
    {868.7,   869.2,   k14dBmErp, 0.001},
    {869.4,   869.65,  k27dBmErp, 0.10},
    {869.7,   870.0,   k14dBmErp, 0.01},
};

// US 902-928 ISM. No duty cycle, but FHSS operation caps channel occupancy at
// 400 ms; see kUs915Note.
constexpr double kUs915Hops[] = {
    903.9, 904.1, 904.3, 904.5, 904.7, 904.9, 905.1, 905.3,
    906.9, 907.1, 907.3, 907.5, 907.7, 907.9, 908.1, 908.3,
    909.9, 910.1, 910.3, 910.5, 910.7, 910.9, 911.1, 911.3,
    912.9, 913.1, 913.3, 913.5, 913.7, 913.9, 914.1, 914.3,
    915.9, 916.1, 916.3, 916.5, 916.7, 916.9, 917.1, 917.3,
    918.9, 919.1, 919.3, 919.5, 919.7, 919.9, 920.1, 920.3,
    921.9, 922.1, 922.3, 922.5, 922.7, 922.9,
};

constexpr double kAu915Hops[] = {
    916.8, 917.0, 917.2, 917.4, 917.6, 917.8, 918.0, 918.2,
    918.4, 918.6, 918.8, 919.0, 919.2, 919.4, 919.6, 919.8,
    920.0, 920.2, 920.4, 920.6, 920.8, 921.0, 921.2, 921.4,
    921.6, 921.8, 922.0, 922.2, 922.4, 922.6, 922.8,
};

constexpr double kAs923Hops[] = {
    922.0, 922.2, 922.4, 922.6, 922.8,
};

constexpr double kKr920Hops[] = {
    922.1, 922.3, 922.5, 922.7, 922.9,
};

constexpr double kEu868Hops[] = {
    868.1, 868.3, 868.5,
};

constexpr const char* kUs915Note =
    "902-928 MHz ISM, no duty cycle. The module only reaches 923 MHz, so the "
    "upper channels of a full US plan are unavailable. FCC 15.247 does not "
    "cleanly cover a stationary single-channel 125 kHz LoRa transmitter: this "
    "firmware therefore hops across the channel plan and caps occupancy at "
    "400 ms per transmission.";

constexpr const char* kEu868Note =
    "EN 300 220. The module starts at 868 MHz, so the 863-868 MHz segment is "
    "out of reach. Duty cycle is per sub-band and strictly enforced.";

constexpr const char* kUnsupportedNote =
    "Outside the Cap LoRa-1262's specified 868-923 MHz range. The hardware "
    "cannot lawfully or usefully operate here.";

constexpr RegionSpec kRegions[] = {
    {Region::EU868, "EU868", "Europe 863-870 MHz", 863.0, 870.0,
     kEu868SubBands, sizeof(kEu868SubBands) / sizeof(kEu868SubBands[0]),
     k14dBmErp, 0.01, 0, false, 869.525, 868.1,
     kEu868Hops, sizeof(kEu868Hops) / sizeof(kEu868Hops[0]), false, true, kEu868Note},

    {Region::UK868, "UK868", "United Kingdom 863-870 MHz", 863.0, 870.0,
     kEu868SubBands, sizeof(kEu868SubBands) / sizeof(kEu868SubBands[0]),
     k14dBmErp, 0.01, 0, false, 869.525, 868.1,
     kEu868Hops, sizeof(kEu868Hops) / sizeof(kEu868Hops[0]), false, true,
     "IR 2030 tracks EN 300 220; limits match EU868."},

    {Region::US915, "US915", "United States 902-928 MHz", 902.0, 928.0,
     nullptr, 0, 30.0, 0.0, 400, false, 906.875, 903.9,
     kUs915Hops, sizeof(kUs915Hops) / sizeof(kUs915Hops[0]), true, true, kUs915Note},

    {Region::AU915, "AU915", "Australia / NZ 915-928 MHz", 915.0, 928.0,
     nullptr, 0, 30.0, 0.0, 0, false, 918.5, 916.8,
     kAu915Hops, sizeof(kAu915Hops) / sizeof(kAu915Hops[0]), false, true,
     "LIPD class licence. The module reaches 915-923 MHz of this band."},

    {Region::AS923, "AS923", "Asia 920-925 MHz", 920.0, 925.0,
     nullptr, 0, 16.0, 0.01, 400, true, 922.1, 922.0,
     kAs923Hops, sizeof(kAs923Hops) / sizeof(kAs923Hops[0]), false, true,
     "Listen-before-talk and a 400 ms dwell cap apply. National variants "
     "differ; verify against your own regulator before transmitting."},

    {Region::KR920, "KR920", "South Korea 920-923.3 MHz", 920.0, 923.3,
     nullptr, 0, 14.0, 0.0, 400, true, 922.1, 922.1,
     kKr920Hops, sizeof(kKr920Hops) / sizeof(kKr920Hops[0]), false, true,
     "Listen-before-talk required."},

    {Region::IN865, "IN865", "India 865-867 MHz", 865.0, 867.0,
     nullptr, 0, 30.0, 0.0, 0, false, 866.55, 865.0625,
     nullptr, 0, false, false, kUnsupportedNote},

    {Region::RU864, "RU864", "Russia 864-870 MHz", 864.0, 870.0,
     nullptr, 0, 16.0, 0.0, 0, false, 869.1, 868.9,
     nullptr, 0, false, false,
     "Only the 868-870 MHz portion is within the module's range."},

    {Region::EU433, "EU433", "433 MHz", 433.05, 434.79,
     nullptr, 0, 12.15, 0.10, 0, false, 433.175, 433.175,
     nullptr, 0, false, false, kUnsupportedNote},
};

static_assert(sizeof(kRegions) / sizeof(kRegions[0]) == static_cast<size_t>(Region::Count),
              "region table must cover every Region enumerator");

const SubBand* findSubBand(const RegionSpec& spec, double freqMhz) {
    for (size_t i = 0; i < spec.subBandCount; ++i) {
        if (freqMhz >= spec.subBands[i].loMhz && freqMhz <= spec.subBands[i].hiMhz) {
            return &spec.subBands[i];
        }
    }
    return nullptr;
}

}  // namespace

const RegionSpec& regionSpec(Region r) {
    const size_t i = static_cast<size_t>(r);
    if (i >= regionCount()) return kRegions[0];
    return kRegions[i];
}

const RegionSpec& regionAt(size_t index) {
    if (index >= regionCount()) return kRegions[0];
    return kRegions[index];
}

const char* regionCode(Region r) { return regionSpec(r).code; }

Region regionFromCode(const char* code) {
    if (code == nullptr) return Region::Count;
    for (size_t i = 0; i < regionCount(); ++i) {
        if (std::strcmp(kRegions[i].code, code) == 0) return kRegions[i].id;
    }
    return Region::Count;
}

bool bandCoverageIsPartial(Region r) {
    const RegionSpec& spec = regionSpec(r);
    return spec.bandLoMhz < kModuleFreqMinMhz || spec.bandHiMhz > kModuleFreqMaxMhz;
}

ChannelVerdict checkChannel(Region r, double freqMhz) {
    ChannelVerdict v;
    const RegionSpec& spec = regionSpec(r);

    if (!spec.moduleSupported) {
        v.reason = "Region is outside the Cap LoRa-1262's 868-923 MHz range.";
        return v;
    }
    if (freqMhz < spec.bandLoMhz || freqMhz > spec.bandHiMhz) {
        v.reason = "Frequency is outside the band allocated in this region.";
        return v;
    }
    if (freqMhz < kModuleFreqMinMhz || freqMhz > kModuleFreqMaxMhz) {
        v.reason = "Frequency is legal here but outside the module's 868-923 MHz range.";
        return v;
    }

    const SubBand* sub = findSubBand(spec, freqMhz);
    if (spec.subBandCount > 0 && sub == nullptr) {
        v.reason = "Frequency falls in a guard gap between allocated sub-bands.";
        return v;
    }

    v.allowed = true;
    v.maxEirpDbm = sub ? sub->maxEirpDbm : spec.defaultEirpDbm;
    v.dutyFraction = sub ? sub->dutyFraction : spec.defaultDutyFraction;
    v.maxDwellMs = spec.maxDwellMs;
    v.requiresLbt = spec.requiresLbt;
    v.reason = "OK";
    return v;
}

size_t hopChannelIndex(Region r, uint64_t unixSeconds, uint32_t periodSeconds) {
    const RegionSpec& spec = regionSpec(r);
    if (spec.hopChannelCount == 0 || unixSeconds == 0 || periodSeconds == 0) return 0;
    return static_cast<size_t>((unixSeconds / periodSeconds) % spec.hopChannelCount);
}

double hopChannelMhz(Region r, uint64_t unixSeconds, uint32_t periodSeconds) {
    const RegionSpec& spec = regionSpec(r);
    if (spec.hopChannelCount == 0) return spec.defaultBeaconMhz;
    return spec.hopChannelsMhz[hopChannelIndex(r, unixSeconds, periodSeconds)];
}

uint32_t hopSecondsRemaining(uint64_t unixSeconds, uint32_t periodSeconds) {
    if (periodSeconds == 0 || unixSeconds == 0) return 0;
    return static_cast<uint32_t>(periodSeconds - (unixSeconds % periodSeconds));
}

int maxConductedDbm(double maxEirpDbm, double antennaGainDbi) {
    // floor(), never round(): rounding up here would authorise a transmitter to
    // exceed the limit by up to half a dB.
    double conducted = std::floor(maxEirpDbm - antennaGainDbi);
    if (conducted > kRadioMaxTxDbm) conducted = kRadioMaxTxDbm;
    if (conducted < kRadioMinTxDbm) conducted = kRadioMinTxDbm;
    return static_cast<int>(conducted);
}

}  // namespace lorascout
