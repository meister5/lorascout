#include "../support/check.h"
#include "airtime.h"

using namespace lorascout;

int main() {
    LoraParams p;  // SF7, 125 kHz, 4/5, 8 preamble, explicit header, CRC on

    // Reference values every LoRaWAN airtime calculator agrees on for a
    // 13-byte payload at BW125 CR4/5.
    CHECK_NEAR(airtimeSeconds(p, 13) * 1000.0, 46.336, 0.01);

    p.spreadingFactor = 12;
    CHECK_NEAR(airtimeSeconds(p, 13) * 1000.0, 1155.072, 0.01);

    // The 18-byte lorascout beacon frame, which sets the survey sample rate.
    CHECK_EQ(airtimeMs(p, 18), 1319u);
    p.spreadingFactor = 7;
    CHECK_EQ(airtimeMs(p, 18), 52u);

    // Rounding is always upward: never under-report occupancy.
    CHECK_TRUE(airtimeMs(p, 18) >= airtimeSeconds(p, 18) * 1000.0);

    // The low-data-rate optimiser is mandatory once a symbol exceeds 16 ms,
    // which at 125 kHz means SF11 and SF12 but not SF10.
    LoraParams q;
    q.bandwidthKhz = 125.0;
    q.spreadingFactor = 10;
    CHECK_FALSE(lowDataRateOptimizeEnabled(q));
    q.spreadingFactor = 11;
    CHECK_TRUE(lowDataRateOptimizeEnabled(q));
    // ...and not at 500 kHz, where the same SF has a short enough symbol.
    q.bandwidthKhz = 500.0;
    CHECK_FALSE(lowDataRateOptimizeEnabled(q));

    // Symbol time doubles with each spreading factor and halves with bandwidth.
    LoraParams r;
    r.spreadingFactor = 7;
    r.bandwidthKhz = 125.0;
    CHECK_NEAR(symbolMs(r), 1.024, 1e-6);
    r.spreadingFactor = 8;
    CHECK_NEAR(symbolMs(r), 2.048, 1e-6);
    r.bandwidthKhz = 250.0;
    CHECK_NEAR(symbolMs(r), 1.024, 1e-6);

    // Airtime must rise monotonically with payload and with spreading factor.
    LoraParams m;
    for (uint8_t sf = 7; sf < 12; ++sf) {
        m.spreadingFactor = sf;
        const uint32_t lo = airtimeMs(m, 20);
        m.spreadingFactor = static_cast<uint8_t>(sf + 1);
        CHECK_TRUE(airtimeMs(m, 20) > lo);
    }
    m.spreadingFactor = 9;
    CHECK_TRUE(airtimeMs(m, 60) > airtimeMs(m, 20));

    // Heavier coding costs airtime.
    LoraParams c45, c48;
    c45.codingRate = 5;
    c48.codingRate = 8;
    CHECK_TRUE(airtimeMs(c48, 32) > airtimeMs(c45, 32));

    return check::finish("airtime");
}
