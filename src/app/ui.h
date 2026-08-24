// Everything drawn on the 240x135 panel.
//
// The screen is feedback, not the deliverable: it exists so you can tell at a
// glance that the survey is working, where the gaps are, and how much transmit
// budget is left. The file on the card is the product.
#pragma once

#include <cstddef>
#include <cstdint>

#include "linkstats.h"
#include "nmea.h"
#include "session.h"
#include "trail.h"

namespace lorascout {
namespace app {

struct RuntimeView {
    Mode mode = Mode::Sweep;
    const char* regionCode = "";
    const char* capName = "";
    const char* presetName = "";

    const GnssFix* fix = nullptr;
    uint32_t fixAgeMs = 0;

    const Trail* trail = nullptr;
    const LinkStats* link = nullptr;

    uint32_t samples = 0;
    uint32_t dropped = 0;
    uint32_t queueDepth = 0;
    uint32_t queueHighWater = 0;

    float lastRssiDbm = 0.0f;
    float lastSnrDb = 0.0f;
    double lastDistanceM = 0.0;
    bool heardAnything = false;

    double freqMhz = 0.0;
    uint8_t sf = 7;
    int txDbm = 0;
    uint32_t txCount = 0;

    // Transmit budget, the number that decides how dense a survey can be.
    double dutyUtilization = 0.0;
    uint32_t minIntervalMs = 0;
    uint32_t nextTxInMs = 0;
    bool dutyBlocked = false;

    bool sdOk = false;
    int batteryPercent = -1;
};

void uiBegin();

// A full-screen refusal. Used when the hardware is not what this firmware is
// for, which is the only condition worth stopping dead over.
void drawFatal(const char* title, const char* detail, const char* hint);

void drawMenu(const char* title, const char* const* items, size_t count,
              size_t selected, const char* footer);

void drawRunning(const RuntimeView& v);

void drawSummary(const SessionInfo& info, const LinkStats& link, const char* path);

void drawBusy(const char* message);

}  // namespace app
}  // namespace lorascout
