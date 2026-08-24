// Detection and power-up of the LoRa cap, including the antenna switch that is
// the single most common reason a -1262 appears to transmit into nothing.
#pragma once

#include <cstdint>

namespace lorascout {
namespace hal {

enum class CapVariant : uint8_t {
    None = 0,     // no cap detected
    LoRa868,      // older Cap LoRa868: no IO expander, no antenna switch
    LoRa1262,     // Cap LoRa-1262: PI4IOE5V6408 at 0x43 fronting FM8625H
};

class Cap {
public:
    // Probes I2C and, on a -1262, enables the antenna path. Must be called
    // before the radio is used for anything.
    bool begin();

    CapVariant variant() const { return variant_; }
    const char* variantName() const;
    bool antennaPathEnabled() const { return antennaEnabled_; }

    // Explicit control, mostly so the antenna can be parked before deep sleep.
    bool setAntennaPath(bool enabled);

private:
    CapVariant variant_ = CapVariant::None;
    bool antennaEnabled_ = false;
};

}  // namespace hal
}  // namespace lorascout
