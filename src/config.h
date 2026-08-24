// Build-time configuration and the Cap-Bus pin map.
#pragma once

#include <Arduino.h>

// This firmware targets the Cardputer ADV specifically. The Cap LoRa-1262 plugs
// into the rear 2x7 Cap-Bus header, which the original Cardputer v1.1 does not
// have; its pin map, keyboard driver and I2C layout differ throughout. Building
// for anything else produces a binary that cannot work.
#if !defined(ARDUINO_ARCH_ESP32)
#error "lorascout targets the ESP32-S3 Cardputer ADV. Select the cardputer-adv PlatformIO environment."
#endif

namespace lorascout {
namespace pins {

// Cap-Bus, per the M5Stack Cap LoRa-1262 documentation.
constexpr int kLoraRst = 3;
constexpr int kLoraIrq = 4;    // DIO1
constexpr int kLoraNss = 5;
constexpr int kLoraBusy = 6;
constexpr int kLoraMosi = 14;
constexpr int kLoraMiso = 39;
constexpr int kLoraSck = 40;

// GNSS UART. Named from the Cardputer's point of view: the host transmits on
// kGnssTx and receives the receiver's NMEA on kGnssRx.
constexpr int kGnssTx = 13;    // -> GPS RX
constexpr int kGnssRx = 15;    // <- GPS TX

// Shared with the HY2.0-4P Grove port.
constexpr int kI2cSda = 8;
constexpr int kI2cScl = 9;

}  // namespace pins

// The PI4IOE5V6408 expander that fronts the FM8625H antenna switch. Its
// presence is also how the -1262 cap is told apart from the older Cap LoRa868.
constexpr uint8_t kIoExpanderAddress = 0x43;
constexpr uint8_t kAntennaSwitchPin = 0;   // P0 drives SX_ANT_SW

constexpr uint32_t kGnssBaud = 115200;

// Sampler -> writer queue. Sized so a microSD flush stalling for a few hundred
// milliseconds costs queue depth rather than measurements.
constexpr size_t kSampleQueueDepth = 64;

// Display geometry of the Cardputer ADV.
constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 135;

}  // namespace lorascout
