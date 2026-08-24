#include "preset.h"

#include <cstring>

namespace lorascout {
namespace {

constexpr RadioPreset kPresets[] = {
    {"Meshtastic LongFast", 0.0, 250.0f, 11, 5, 0x2B, 16, true, false,
     "Meshtastic's default channel. Receive only: this firmware will not "
     "transmit into someone else's mesh."},
    {"Meshtastic MediumFast", 0.0, 250.0f, 9, 5, 0x2B, 16, true, false,
     "Receive only."},
    {"Meshtastic ShortFast", 0.0, 250.0f, 7, 5, 0x2B, 16, true, false,
     "Receive only."},
    {"LoRaWAN SF7BW125", 0.0, 125.0f, 7, 5, 0x34, 8, true, false,
     "LoRaWAN uplink, fastest data rate. Receive only."},
    {"LoRaWAN SF9BW125", 0.0, 125.0f, 9, 5, 0x34, 8, true, false,
     "LoRaWAN uplink, mid data rate. Receive only."},
    {"LoRaWAN SF12BW125", 0.0, 125.0f, 12, 5, 0x34, 8, true, false,
     "LoRaWAN uplink, slowest and longest range. Receive only."},
    {"lorascout survey", 0.0, 125.0f, 7, 5, kLorascoutSyncWord, 8, true, true,
     "Beacon and rover default. Fast enough that the duty-cycle budget still "
     "permits a sample every few seconds."},
    {"lorascout long range", 0.0, 125.0f, 10, 5, kLorascoutSyncWord, 8, true, true,
     "For reaching the edge of coverage. Costs roughly eight times the airtime "
     "of the survey preset, so samples come correspondingly further apart."},
};

constexpr size_t kBeaconIndex = 6;
constexpr size_t kBeaconLongIndex = 7;

static_assert(kPresets[kBeaconIndex].transmitAllowed, "beacon preset must permit transmit");
static_assert(kPresets[kBeaconIndex].syncWord == kLorascoutSyncWord,
              "beacon must use lorascout's own sync word");

}  // namespace

size_t presetCount() { return sizeof(kPresets) / sizeof(kPresets[0]); }

const RadioPreset& presetAt(size_t index) {
    if (index >= presetCount()) return kPresets[0];
    return kPresets[index];
}

const RadioPreset* presetByName(const char* name) {
    if (name == nullptr) return nullptr;
    for (size_t i = 0; i < presetCount(); ++i) {
        if (std::strcmp(kPresets[i].name, name) == 0) return &kPresets[i];
    }
    return nullptr;
}

const RadioPreset& beaconPreset() { return kPresets[kBeaconIndex]; }
const RadioPreset& beaconLongRangePreset() { return kPresets[kBeaconLongIndex]; }

double presetFrequency(const RadioPreset& p, Region r) {
    if (p.freqMhz > 0.0) return p.freqMhz;
    const RegionSpec& spec = regionSpec(r);
    return p.transmitAllowed ? spec.defaultBeaconMhz : spec.defaultListenMhz;
}

}  // namespace lorascout
