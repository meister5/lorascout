#include "airtime.h"

#include <cmath>

namespace lorascout {

bool lowDataRateOptimizeEnabled(const LoraParams& p) {
    if (!p.lowDataRateOptimizeAuto) return p.lowDataRateOptimize;
    // The optimiser is mandated when symbol time exceeds 16 ms.
    return symbolMs(p) > 16.0;
}

double symbolMs(const LoraParams& p) {
    const double bwHz = p.bandwidthKhz * 1000.0;
    return (std::pow(2.0, static_cast<double>(p.spreadingFactor)) / bwHz) * 1000.0;
}

double airtimeSeconds(const LoraParams& p, uint8_t payloadBytes) {
    const double tSym = symbolMs(p) / 1000.0;
    const double tPreamble = (static_cast<double>(p.preambleSymbols) + 4.25) * tSym;

    const int sf = p.spreadingFactor;
    const int de = lowDataRateOptimizeEnabled(p) ? 1 : 0;
    const int ih = p.explicitHeader ? 0 : 1;
    const int crc = p.crcEnabled ? 1 : 0;
    const int cr = static_cast<int>(p.codingRate) - 4;  // 1..4

    const double numerator = 8.0 * payloadBytes - 4.0 * sf + 28.0 + 16.0 * crc - 20.0 * ih;
    const double denominator = 4.0 * (sf - 2 * de);

    double symbols = 8.0;
    if (denominator > 0.0) {
        double n = std::ceil(numerator / denominator) * (cr + 4);
        if (n < 0.0) n = 0.0;
        symbols += n;
    }

    return tPreamble + symbols * tSym;
}

uint32_t airtimeMs(const LoraParams& p, uint8_t payloadBytes) {
    return static_cast<uint32_t>(std::ceil(airtimeSeconds(p, payloadBytes) * 1000.0));
}

}  // namespace lorascout
