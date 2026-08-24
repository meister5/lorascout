#include "settings.h"

#include <Preferences.h>
#include <esp_mac.h>

namespace lorascout {
namespace app {
namespace {
constexpr const char* kNamespace = "lorascout";
}

void Settings::load() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) return;

    const uint8_t r = prefs.getUChar("region", static_cast<uint8_t>(Region::Count));
    region = r < static_cast<uint8_t>(Region::Count) ? static_cast<Region>(r) : Region::Count;

    antennaGainDbi = prefs.getDouble("antgain", kStockAntennaGainDbi);
    listenPresetIndex = prefs.getUChar("preset", 0);
    rotatePresets = prefs.getBool("rotate", true);
    beaconSf = prefs.getUChar("bsf", 7);
    nodeId = prefs.getUShort("node", 0);
    payloadCapture = prefs.getBool("payload", false);
    hopping = prefs.getBool("hop", false);
    prefs.end();

    // A region stored by an older build might not be one this build can use.
    if (region != Region::Count && !regionSpec(region).moduleSupported) {
        region = Region::Count;
    }
    if (beaconSf < 7 || beaconSf > 12) beaconSf = 7;
}

void Settings::save() const {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return;
    prefs.putUChar("region", static_cast<uint8_t>(region));
    prefs.putDouble("antgain", antennaGainDbi);
    prefs.putUChar("preset", listenPresetIndex);
    prefs.putBool("rotate", rotatePresets);
    prefs.putUChar("bsf", beaconSf);
    prefs.putUShort("node", nodeId);
    prefs.putBool("payload", payloadCapture);
    prefs.putBool("hop", hopping);
    prefs.end();
}

uint16_t Settings::effectiveNodeId() const {
    if (nodeId != 0) return nodeId;
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return 1;
    const uint16_t id = static_cast<uint16_t>((mac[4] << 8) | mac[5]);
    // 0 is reserved for "unset", so a MAC that happens to end in zeros still
    // produces a usable id.
    return id == 0 ? 1 : id;
}

}  // namespace app
}  // namespace lorascout
