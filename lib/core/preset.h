// Radio presets for listen mode, plus lorascout's own beacon parameters.
//
// LoRa is not scannable. To hear a frame you must already match its spreading
// factor, bandwidth, coding rate and sync word, so passive listening is always
// listening for something specific. These presets are the shortlist worth
// trying; the UI rotates through a selection of them, trading dwell time for
// coverage, and says so rather than pretending to be a spectrum analyser.
#pragma once

#include <cstddef>
#include <cstdint>

#include "region.h"

namespace lorascout {

// lorascout's own sync word, deliberately distinct from LoRaWAN's public 0x34,
// its private 0x12, and Meshtastic's 0x2B. Beacon mode must never be able to
// inject a frame that another network will accept as its own.
constexpr uint8_t kLorascoutSyncWord = 0x5C;

struct RadioPreset {
    const char* name;
    // 0 means "use the region's default listen frequency", which is how a
    // single preset table serves every region.
    double freqMhz;
    float bwKhz;
    uint8_t sf;
    uint8_t cr;
    uint8_t syncWord;
    uint16_t preambleSymbols;
    bool crcEnabled;
    // True when this preset describes a network we may transmit on. Everything
    // we merely listen to is receive-only, always.
    bool transmitAllowed;
    const char* note;
};

size_t presetCount();
const RadioPreset& presetAt(size_t index);
// Returns nullptr when the name is unknown.
const RadioPreset* presetByName(const char* name);

// The beacon preset, which is the only one transmit is ever permitted on.
const RadioPreset& beaconPreset();
const RadioPreset& beaconLongRangePreset();

// Resolves a preset's frequency for a region, applying the region default when
// the preset does not pin one.
double presetFrequency(const RadioPreset& p, Region r);

}  // namespace lorascout
