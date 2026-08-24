#include "../support/check.h"
#include "region.h"

using namespace lorascout;

int main() {
    // EU sub-band limits, including the high-power 869.4-869.65 window that is
    // the reason sub-bands are modelled at all.
    ChannelVerdict eu = checkChannel(Region::EU868, 868.1);
    CHECK_TRUE(eu.allowed);
    CHECK_NEAR(eu.maxEirpDbm, 16.15, 0.001);   // 14 dBm ERP
    CHECK_NEAR(eu.dutyFraction, 0.01, 1e-9);

    ChannelVerdict euHigh = checkChannel(Region::EU868, 869.5);
    CHECK_TRUE(euHigh.allowed);
    CHECK_NEAR(euHigh.maxEirpDbm, 29.15, 0.001);  // 27 dBm ERP
    CHECK_NEAR(euHigh.dutyFraction, 0.10, 1e-9);

    ChannelVerdict euLowDuty = checkChannel(Region::EU868, 869.0);
    CHECK_TRUE(euLowDuty.allowed);
    CHECK_NEAR(euLowDuty.dutyFraction, 0.001, 1e-9);

    // 869.2-869.4 is a gap between allocations, not a usable channel.
    CHECK_FALSE(checkChannel(Region::EU868, 869.3).allowed);

    // Legal in the EU, but below what the module is specified for.
    ChannelVerdict below = checkChannel(Region::EU868, 866.0);
    CHECK_FALSE(below.allowed);
    CHECK_CONTAINS(below.reason, "module");

    // Outside the band entirely.
    CHECK_FALSE(checkChannel(Region::EU868, 871.0).allowed);

    // US: no duty cycle, but a dwell cap, and the top of the band is out of
    // module range.
    ChannelVerdict us = checkChannel(Region::US915, 903.9);
    CHECK_TRUE(us.allowed);
    CHECK_NEAR(us.dutyFraction, 0.0, 1e-9);
    CHECK_EQ(us.maxDwellMs, 400u);
    CHECK_FALSE(checkChannel(Region::US915, 927.0).allowed);
    CHECK_FALSE(checkChannel(Region::US915, 901.0).allowed);

    // Listen-before-talk regions must advertise it.
    CHECK_TRUE(checkChannel(Region::KR920, 922.1).requiresLbt);
    CHECK_TRUE(checkChannel(Region::AS923, 922.0).requiresLbt);
    CHECK_FALSE(checkChannel(Region::EU868, 868.1).requiresLbt);

    // Regions the hardware simply cannot serve are refused with an explanation,
    // never silently allowed.
    for (double f = 860.0; f < 940.0; f += 0.1) {
        CHECK_FALSE(checkChannel(Region::EU433, f).allowed);
        CHECK_FALSE(checkChannel(Region::IN865, f).allowed);
    }
    CHECK_CONTAINS(checkChannel(Region::EU433, 433.1).reason, "868-923");

    // No region may ever authorise a frequency outside the module's range.
    for (size_t i = 0; i < regionCount(); ++i) {
        const RegionSpec& spec = regionAt(i);
        for (double f = 400.0; f < 960.0; f += 0.05) {
            ChannelVerdict v = checkChannel(spec.id, f);
            if (v.allowed) {
                CHECK_TRUE(f >= kModuleFreqMinMhz && f <= kModuleFreqMaxMhz);
            }
        }
        // Every declared hop channel must itself pass the check.
        for (size_t h = 0; h < spec.hopChannelCount; ++h) {
            CHECK_TRUE(checkChannel(spec.id, spec.hopChannelsMhz[h]).allowed);
        }
        // Defaults must be usable on supported regions.
        if (spec.moduleSupported) {
            CHECK_TRUE(checkChannel(spec.id, spec.defaultBeaconMhz).allowed);
            CHECK_TRUE(checkChannel(spec.id, spec.defaultListenMhz).allowed);
        }
    }

    // Conducted power backs off by the antenna gain and never rounds upward.
    CHECK_EQ(maxConductedDbm(16.15, kStockAntennaGainDbi), 13);
    CHECK_EQ(maxConductedDbm(16.15, 0.0), 16);
    // A generous limit still clamps to what the PA can deliver.
    CHECK_EQ(maxConductedDbm(29.15, kStockAntennaGainDbi), kRadioMaxTxDbm);
    CHECK_EQ(maxConductedDbm(30.0, 0.0), kRadioMaxTxDbm);
    // A high-gain antenna forces a real reduction.
    CHECK_EQ(maxConductedDbm(16.15, 9.0), 7);
    // Absurd gain floors at the PA minimum rather than going negative-infinite.
    CHECK_EQ(maxConductedDbm(16.15, 40.0), kRadioMinTxDbm);

    CHECK_TRUE(bandCoverageIsPartial(Region::EU868));
    CHECK_TRUE(bandCoverageIsPartial(Region::US915));

    // Only the US plan legally requires a stationary transmitter to hop.
    CHECK_TRUE(regionSpec(Region::US915).requiresHopping);
    CHECK_FALSE(regionSpec(Region::EU868).requiresHopping);

    // Hop scheduling: both ends derive the same channel from GNSS time alone.
    {
        const uint32_t period = 4;
        const RegionSpec& us = regionSpec(Region::US915);
        const uint64_t t = 1787924702ull;
        const size_t idx = hopChannelIndex(Region::US915, t, period);
        CHECK_EQ(idx, (t / period) % us.hopChannelCount);
        // Stable across the whole dwell, and advancing by one at the boundary.
        const uint64_t slotStart = (t / period) * period;
        CHECK_EQ(hopChannelIndex(Region::US915, slotStart, period), idx);
        CHECK_EQ(hopChannelIndex(Region::US915, slotStart + period - 1, period), idx);
        CHECK_EQ(hopChannelIndex(Region::US915, slotStart + period, period),
                 (idx + 1) % us.hopChannelCount);
        // Whatever the schedule picks must itself be a legal channel.
        for (uint64_t k = 0; k < 200; ++k) {
            const double f3 = hopChannelMhz(Region::US915, slotStart + k, period);
            CHECK_TRUE(checkChannel(Region::US915, f3).allowed);
        }
        // Without a time fix there is no schedule, so a receiver parks on the
        // first channel rather than guessing.
        CHECK_EQ(hopChannelIndex(Region::US915, 0, period), 0u);
        CHECK_EQ(hopChannelIndex(Region::EU868, t, 0), 0u);
        // A region with no plan falls back to its default channel.
        CHECK_NEAR(hopChannelMhz(Region::IN865, t, period),
                   regionSpec(Region::IN865).defaultBeaconMhz, 1e-9);

        CHECK_EQ(hopSecondsRemaining(slotStart, period), period);
        CHECK_EQ(hopSecondsRemaining(slotStart + 1, period), period - 1);
        CHECK_EQ(hopSecondsRemaining(0, period), 0u);
    }

    CHECK_STREQ(regionCode(Region::US915), "US915");
    CHECK_TRUE(regionFromCode("EU868") == Region::EU868);
    CHECK_TRUE(regionFromCode("NOPE") == Region::Count);
    CHECK_TRUE(regionFromCode(nullptr) == Region::Count);

    return check::finish("region");
}
