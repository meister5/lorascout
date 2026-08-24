// One classification of signal strength, shared by the on-screen trail and by
// every exported file. Defining it once is what makes the colours on the device
// and the colours in QGIS mean the same thing.
#pragma once

#include <cstddef>
#include <cstdint>

namespace lorascout {

enum class SignalBand : uint8_t {
    Excellent = 0,
    Good,
    Fair,
    Weak,
    Marginal,
    None,       // nothing heard at all
    Count,
};

struct SignalStyle {
    const char* label;
    uint16_t rgb565;      // for M5GFX on the device
    const char* cssHex;   // for GeoJSON / geojson.io
    const char* kmlAbgr;  // KML is aabbggrr, not rrggbb
};

// Thresholds are in dBm at the receiver. They are set against the SX1262's
// -147 dBm floor at SF12: "marginal" still decodes at low data rates, it just
// has no margin left for a tree.
constexpr double kRssiExcellentDbm = -80.0;
constexpr double kRssiGoodDbm = -95.0;
constexpr double kRssiFairDbm = -105.0;
constexpr double kRssiWeakDbm = -115.0;

SignalBand classifyRssi(double rssiDbm);
const SignalStyle& signalStyle(SignalBand band);

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

}  // namespace lorascout
