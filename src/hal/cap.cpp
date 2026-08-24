#include "cap.h"

#include <M5Unified.h>

#include "utility/PI4IOE5V6408_Class.hpp"

#include "../config.h"

namespace lorascout {
namespace hal {
namespace {
m5::PI4IOE5V6408_Class g_ioe(kIoExpanderAddress, 400000, &m5::In_I2C);
}  // namespace

bool Cap::begin() {
    if (!m5::In_I2C.begin()) {
        variant_ = CapVariant::None;
        return false;
    }

    if (g_ioe.begin()) {
        variant_ = CapVariant::LoRa1262;
        return setAntennaPath(true);
    }

    // No expander. Either the older Cap LoRa868, whose SX1276 path needs no
    // switch, or no cap at all. The radio probe that follows decides which.
    variant_ = CapVariant::LoRa868;
    antennaEnabled_ = true;   // nothing to switch
    return true;
}

bool Cap::setAntennaPath(bool enabled) {
    if (variant_ != CapVariant::LoRa1262) {
        antennaEnabled_ = true;
        return true;
    }
    // P0 must be an output *and* out of high impedance before it can drive the
    // switch. Setting direction alone leaves the pin floating and the radio
    // transmitting into a disconnected antenna port.
    g_ioe.setDirection(kAntennaSwitchPin, true);
    g_ioe.setHighImpedance(kAntennaSwitchPin, false);
    g_ioe.digitalWrite(kAntennaSwitchPin, enabled);
    antennaEnabled_ = enabled;
    return true;
}

const char* Cap::variantName() const {
    switch (variant_) {
        case CapVariant::LoRa1262: return "Cap LoRa-1262";
        case CapVariant::LoRa868: return "Cap LoRa868";
        case CapVariant::None:
        default: return "none";
    }
}

}  // namespace hal
}  // namespace lorascout
