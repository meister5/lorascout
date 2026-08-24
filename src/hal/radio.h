// Thin wrapper over RadioLib's SX1262 driver.
//
// Deliberately dumb: it configures, it receives, it transmits, it measures.
// Every decision about whether a transmission is permitted is made in lib/core
// and passed down as an already-clamped power level, because that logic has to
// be testable on the host and this layer cannot be.
#pragma once

#include <cstddef>
#include <cstdint>

#include "sample.h"

namespace lorascout {
namespace hal {

struct RxPacket {
    uint8_t data[256] = {};
    size_t length = 0;
    float rssiDbm = 0.0f;
    float snrDb = 0.0f;
    float freqErrorHz = 0.0f;
    bool crcOk = false;
};

class Radio {
public:
    bool begin();
    bool applyConfig(const RadioConfig& cfg);
    const RadioConfig& config() const { return config_; }

    // Conducted power in dBm. The caller is responsible for having clamped this
    // through region limits and antenna gain first.
    bool setTxPower(int dbm);
    int txPower() const { return txDbm_; }

    bool startReceive();
    bool standby();

    // True when DIO1 has fired since the last drain.
    bool packetPending() const;
    bool readPacket(RxPacket* out);

    // Blocking transmit. Returns false on radio error; the caller must already
    // have cleared the duty-cycle and dwell checks.
    bool transmit(const uint8_t* data, size_t len);

    // Instantaneous channel RSSI in dBm, for the noise-floor sweep.
    float readRssi();
    // Channel activity detection: true when LoRa preamble energy is present.
    // Used both for the sweep's busy flag and for listen-before-talk.
    bool channelBusy();

    const char* lastError() const { return lastError_; }

private:
    RadioConfig config_;
    int txDbm_ = 0;
    bool started_ = false;
    const char* lastError_ = "";
};

}  // namespace hal
}  // namespace lorascout
