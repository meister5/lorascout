#include "radio.h"

#include <RadioLib.h>

#include "../config.h"

namespace lorascout {
namespace hal {
namespace {

// NSS, DIO1, RESET, BUSY. RadioLib takes MOSI/MISO/SCK from the board defaults
// that M5Unified installs, which on the Cardputer ADV are G14/G39/G40.
SX1262 g_radio = new Module(pins::kLoraNss, pins::kLoraIrq, pins::kLoraRst, pins::kLoraBusy);

volatile bool g_packetFlag = false;

// Kept as short as an ISR should be: set a flag, let the sampler task drain it.
ICACHE_RAM_ATTR void onDio1() { g_packetFlag = true; }

}  // namespace

bool Radio::begin() {
    const int16_t state = g_radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        lastError_ = "SX1262 begin failed";
        return false;
    }
    g_radio.setDio1Action(onDio1);
    started_ = true;
    return true;
}

bool Radio::applyConfig(const RadioConfig& cfg) {
    if (!started_) return false;

    int16_t state = g_radio.setFrequency(static_cast<float>(cfg.freqMhz));
    if (state != RADIOLIB_ERR_NONE) { lastError_ = "frequency rejected"; return false; }

    state = g_radio.setBandwidth(cfg.bwKhz);
    if (state != RADIOLIB_ERR_NONE) { lastError_ = "bandwidth rejected"; return false; }

    state = g_radio.setSpreadingFactor(cfg.sf);
    if (state != RADIOLIB_ERR_NONE) { lastError_ = "spreading factor rejected"; return false; }

    state = g_radio.setCodingRate(cfg.cr);
    if (state != RADIOLIB_ERR_NONE) { lastError_ = "coding rate rejected"; return false; }

    state = g_radio.setSyncWord(cfg.syncWord);
    if (state != RADIOLIB_ERR_NONE) { lastError_ = "sync word rejected"; return false; }

    state = g_radio.setPreambleLength(cfg.preambleSymbols);
    if (state != RADIOLIB_ERR_NONE) { lastError_ = "preamble rejected"; return false; }

    // The 1262 module carries no TCXO of its own; leaving the DIO3 reference on
    // would keep an absent oscillator powered and stall every operation.
    g_radio.setTCXO(0);
    g_radio.setCurrentLimit(140.0f);

    config_ = cfg;
    return true;
}

bool Radio::setTxPower(int dbm) {
    if (!started_) return false;
    const int16_t state = g_radio.setOutputPower(static_cast<int8_t>(dbm));
    if (state != RADIOLIB_ERR_NONE) {
        lastError_ = "output power rejected";
        return false;
    }
    txDbm_ = dbm;
    return true;
}

bool Radio::startReceive() {
    if (!started_) return false;
    g_packetFlag = false;
    return g_radio.startReceive() == RADIOLIB_ERR_NONE;
}

bool Radio::standby() {
    if (!started_) return false;
    return g_radio.standby() == RADIOLIB_ERR_NONE;
}

bool Radio::packetPending() const { return g_packetFlag; }

bool Radio::readPacket(RxPacket* out) {
    if (!started_ || out == nullptr || !g_packetFlag) return false;
    g_packetFlag = false;

    const size_t len = g_radio.getPacketLength();
    if (len == 0 || len > sizeof(out->data)) {
        g_radio.startReceive();
        return false;
    }

    const int16_t state = g_radio.readData(out->data, len);
    out->length = len;
    out->rssiDbm = g_radio.getRSSI();
    out->snrDb = g_radio.getSNR();
    out->freqErrorHz = g_radio.getFrequencyError();
    // A CRC mismatch is still a real measurement: the frame reached us, it just
    // did not survive. Recording it is how the edge of coverage gets mapped.
    out->crcOk = (state == RADIOLIB_ERR_NONE);

    g_radio.startReceive();
    return state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_CRC_MISMATCH;
}

bool Radio::transmit(const uint8_t* data, size_t len) {
    if (!started_ || data == nullptr || len == 0) return false;
    const int16_t state = g_radio.transmit(const_cast<uint8_t*>(data), len);
    if (state != RADIOLIB_ERR_NONE) {
        lastError_ = "transmit failed";
        return false;
    }
    return true;
}

float Radio::readRssi() {
    if (!started_) return 0.0f;
    return g_radio.getRSSI(false);
}

bool Radio::channelBusy() {
    if (!started_) return false;
    const int16_t state = g_radio.scanChannel();
    return state == RADIOLIB_LORA_DETECTED || state == RADIOLIB_PREAMBLE_DETECTED;
}

}  // namespace hal
}  // namespace lorascout
