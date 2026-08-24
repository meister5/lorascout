#include "signalband.h"

namespace lorascout {
namespace {
constexpr SignalStyle kStyles[] = {
    {"excellent", rgb565(0x2E, 0xCC, 0x71), "#2ecc71", "ff71cc2e"},
    {"good",      rgb565(0xA3, 0xD6, 0x3C), "#a3d63c", "ff3cd6a3"},
    {"fair",      rgb565(0xF1, 0xC4, 0x0F), "#f1c40f", "ff0fc4f1"},
    {"weak",      rgb565(0xE6, 0x7E, 0x22), "#e67e22", "ff227ee6"},
    {"marginal",  rgb565(0xE7, 0x4C, 0x3C), "#e74c3c", "ff3c4ce7"},
    {"none",      rgb565(0x55, 0x5A, 0x66), "#555a66", "ff665a55"},
};
static_assert(sizeof(kStyles) / sizeof(kStyles[0]) == static_cast<size_t>(SignalBand::Count),
              "one style per band");
}  // namespace

SignalBand classifyRssi(double rssiDbm) {
    if (rssiDbm >= kRssiExcellentDbm) return SignalBand::Excellent;
    if (rssiDbm >= kRssiGoodDbm) return SignalBand::Good;
    if (rssiDbm >= kRssiFairDbm) return SignalBand::Fair;
    if (rssiDbm >= kRssiWeakDbm) return SignalBand::Weak;
    return SignalBand::Marginal;
}

const SignalStyle& signalStyle(SignalBand band) {
    const size_t i = static_cast<size_t>(band);
    if (i >= static_cast<size_t>(SignalBand::Count)) return kStyles[static_cast<size_t>(SignalBand::None)];
    return kStyles[i];
}

}  // namespace lorascout
