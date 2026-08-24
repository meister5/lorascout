// LoRa time-on-air. This is the number every compliance decision hangs off:
// duty cycle, dwell limits, and how often the beacon is legally allowed to
// speak all reduce to "how long does this frame occupy the channel".
#pragma once

#include <cstdint>

namespace lorascout {

struct LoraParams {
    uint8_t spreadingFactor = 7;   // 5..12 on SX126x
    double bandwidthKhz = 125.0;
    uint8_t codingRate = 5;        // 5..8, meaning 4/5 .. 4/8
    uint16_t preambleSymbols = 8;
    bool explicitHeader = true;
    bool crcEnabled = true;
    // SF11/SF12 at 125 kHz require the low-data-rate optimiser; at wider
    // bandwidths they do not. Leaving this on "auto" avoids a class of airtime
    // miscalculation that would quietly under-report duty cycle usage.
    bool lowDataRateOptimizeAuto = true;
    bool lowDataRateOptimize = false;
};

bool lowDataRateOptimizeEnabled(const LoraParams& p);

// Semtech's time-on-air formula (SX1276 datasheet 4.1.1.7, unchanged for
// SX126x LoRa). Returns milliseconds, rounded up: never under-report airtime,
// because under-reporting is what turns a duty-cycle budget into a violation.
uint32_t airtimeMs(const LoraParams& p, uint8_t payloadBytes);

double airtimeSeconds(const LoraParams& p, uint8_t payloadBytes);

// Symbol duration in milliseconds.
double symbolMs(const LoraParams& p);

}  // namespace lorascout
