// Persisted configuration, stored in NVS.
//
// Region is deliberately *not* defaulted. A radio that picks a region for you
// is a radio that transmits on someone else's allocation, so first boot asks
// and refuses to go further until it is answered.
#pragma once

#include <cstdint>

#include "region.h"

namespace lorascout {
namespace app {

struct Settings {
    Region region = Region::Count;   // Count means "not yet chosen"
    double antennaGainDbi = kStockAntennaGainDbi;
    uint8_t listenPresetIndex = 0;
    bool rotatePresets = true;
    uint8_t beaconSf = 7;
    uint16_t nodeId = 0;             // 0 means "derive from the MAC"
    bool payloadCapture = false;
    bool hopping = false;

    bool regionChosen() const { return region != Region::Count; }

    void load();
    void save() const;

    // Derives a stable per-device id from the ESP32 MAC when none is set.
    uint16_t effectiveNodeId() const;
};

}  // namespace app
}  // namespace lorascout
