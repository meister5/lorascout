#include "radio.h"

#include <RadioLib.h>
#include <SPI.h>

#include <cstdio>

#include "../config.h"

namespace lorascout {
namespace hal {
namespace {

// NSS, DIO1, RESET, BUSY, and the SPI bus, named explicitly.
//
// Handing RadioLib the bus object matters twice over. The Module constructor
// that omits it leaves RadioLib to call SPI.begin() with no arguments, which
// lands on the ESP32-S3 variant defaults -- SCK 12, MISO 13, MOSI 11 -- and not
// on the Cap-Bus pins the cap is wired to, so the SX1262 never answers and
// begin() returns RADIOLIB_ERR_CHIP_NOT_FOUND. It also stops RadioLib calling
// SPI.end() on a failed probe, which would pull the bus out from under the
// microSD slot that shares it.
SX1262 g_radio = new Module(pins::kLoraNss, pins::kLoraIrq, pins::kLoraRst, pins::kLoraBusy, SPI);

volatile bool g_packetFlag = false;

// Kept as short as an ISR should be: set a flag, let the sampler task drain it.
ICACHE_RAM_ATTR void onDio1() { g_packetFlag = true; }

}  // namespace

bool Radio::begin() {
    // The Cap-Bus SPI pins are the microSD slot's pins: on the ADV both hang off
    // SCK 40 / MOSI 14 / MISO 39 with separate chip selects (LoRa 5, card 12).
    // Whichever peripheral comes up first owns the bus -- SPI.begin() returns
    // early once the host is running -- and the radio is probed before storage,
    // so the pin names belong here. -1 for SS: neither driver wants the SPI
    // peripheral's hardware CS, they drive their own.
    SPI.begin(pins::kLoraSck, pins::kLoraMiso, pins::kLoraMosi, -1);

    const int16_t state = g_radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        // The numeric code is the difference between "cap not seated" (-2,
        // chip not found) and a chip that answers but rejects something, so it
        // goes in front of the user rather than into a debug build only.
        std::snprintf(errorBuf_, sizeof(errorBuf_),
                      "SX1262 begin failed (RadioLib %d)", static_cast<int>(state));
        lastError_ = errorBuf_;
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

    // No setTCXO() here. RadioLib's begin() already probes the reference: it
    // configures DIO3 for a TCXO, and if the chip comes back with
    // XOSC_START_ERR -- an XTAL module being told it has a TCXO -- it drops to
    // 0 V and reconfigures itself. Calling setTCXO(0) afterwards is not a
    // hint, it is reset(true): a hard chip reset that discards every setting
    // applied above and leaves the radio on RadioLib's 434 MHz defaults.
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
