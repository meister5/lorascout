#include "ui.h"

#include <M5Unified.h>

#include <cstdio>

#include "../config.h"
#include "exporters.h"
#include "signalband.h"

namespace lorascout {
namespace app {
namespace {

constexpr uint16_t kBg = rgb565(0x10, 0x12, 0x18);
constexpr uint16_t kPanel = rgb565(0x1B, 0x1F, 0x28);
constexpr uint16_t kInk = rgb565(0xE6, 0xE9, 0xEF);
constexpr uint16_t kMuted = rgb565(0x8A, 0x93, 0xA3);
constexpr uint16_t kAccent = rgb565(0x4A, 0xA3, 0xFF);
constexpr uint16_t kWarn = rgb565(0xF1, 0xC4, 0x0F);
constexpr uint16_t kDanger = rgb565(0xE7, 0x4C, 0x3C);

constexpr int kHeaderH = 14;
constexpr int kFooterH = 12;

M5Canvas g_canvas(&M5.Display);

void text(int x, int y, uint16_t colour, const char* s) {
    g_canvas.setTextColor(colour);
    g_canvas.drawString(s, x, y);
}

void textf(int x, int y, uint16_t colour, const char* fmt, ...) {
    char buf[96];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    text(x, y, colour, buf);
}

const char* fixLabel(const GnssFix* fix) {
    if (fix == nullptr || !fix->valid) return "NO FIX";
    switch (fix->fixType) {
        case FixType::Fix3D: return "3D";
        case FixType::Fix2D: return "2D";
        default: return "NO FIX";
    }
}

uint16_t fixColour(const GnssFix* fix) {
    if (fix == nullptr || !fix->valid) return kDanger;
    if (!fix->usableForSurvey()) return kWarn;
    return rgb565(0x2E, 0xCC, 0x71);
}

void drawHeader(const RuntimeView& v) {
    g_canvas.fillRect(0, 0, kScreenWidth, kHeaderH, kPanel);

    char mode[16];
    snprintf(mode, sizeof(mode), "%s", modeName(v.mode));
    for (char* p = mode; *p; ++p) *p = static_cast<char>(toupper(*p));
    text(3, 3, kAccent, mode);

    textf(46, 3, kMuted, "%s %.3f", v.regionCode, v.freqMhz);

    // Fix state sits top-right because it is the thing that invalidates
    // everything else on the screen.
    const char* label = fixLabel(v.fix);
    const uint16_t colour = fixColour(v.fix);
    if (v.fix != nullptr && v.fix->valid) {
        textf(kScreenWidth - 62, 3, colour, "%s %u/%u", label, v.fix->satsUsed,
              v.fix->satsInView);
    } else {
        text(kScreenWidth - 62, 3, colour, label);
    }

    if (!v.sdOk) text(kScreenWidth - 78, 3, kDanger, "!SD");
}

void drawFooter(const RuntimeView& v) {
    const int y = kScreenHeight - kFooterH;
    g_canvas.fillRect(0, y, kScreenWidth, kFooterH, kPanel);

    if (!modeTransmits(v.mode)) {
        textf(3, y + 2, kMuted, "%u samples  drop %u  q%u/%u", v.samples, v.dropped,
              v.queueDepth, static_cast<unsigned>(kSampleQueueDepth));
        return;
    }

    // Transmit budget as a bar. This is the constraint that decides how dense a
    // survey can be, so it gets permanent screen space rather than a menu.
    const int barW = 60;
    const int filled = static_cast<int>(v.dutyUtilization * barW + 0.5);
    const uint16_t colour = v.dutyUtilization > 0.9 ? kDanger
                          : v.dutyUtilization > 0.6 ? kWarn
                                                    : rgb565(0x2E, 0xCC, 0x71);
    g_canvas.drawRect(3, y + 3, barW, 6, kMuted);
    if (filled > 0) g_canvas.fillRect(4, y + 4, filled > barW - 2 ? barW - 2 : filled, 4, colour);

    if (v.dutyBlocked) {
        textf(barW + 8, y + 2, kDanger, "HOLD %us  tx %u", v.nextTxInMs / 1000, v.txCount);
    } else {
        textf(barW + 8, y + 2, kMuted, "every %.1fs  tx %u  %+ddBm",
              v.minIntervalMs / 1000.0, v.txCount, v.txDbm);
    }
}

void drawTrailPanel(const RuntimeView& v, int x, int y, int w, int h) {
    g_canvas.drawRect(x, y, w, h, kPanel);
    if (v.trail == nullptr || v.trail->empty()) {
        text(x + 6, y + h / 2 - 4, kMuted, "waiting for fix");
        return;
    }

    const Trail& trail = *v.trail;
    Trail::Pixel prev{};
    bool havePrev = false;

    for (size_t i = 0; i < trail.size(); ++i) {
        const Trail::Point& p = trail.at(i);
        Trail::Pixel px;
        if (!trail.project(p.coord, w, h, 5, &px)) continue;
        px.x += x;
        px.y += y;

        if (havePrev) {
            g_canvas.drawLine(prev.x, prev.y, px.x, px.y, rgb565(0x3A, 0x40, 0x4C));
        }
        prev = px;
        havePrev = true;

        g_canvas.fillCircle(px.x, px.y, 2, signalStyle(p.band).rgb565);
    }

    // Current position, drawn last so it is never hidden under the trail.
    if (havePrev) {
        g_canvas.drawCircle(prev.x, prev.y, 4, kInk);
    }

    const double span = trail.spanMeters();
    if (span > 0.0) {
        char label[24];
        if (span >= 1000.0) {
            snprintf(label, sizeof(label), "%.1f km", span / 1000.0);
        } else {
            snprintf(label, sizeof(label), "%.0f m", span);
        }
        text(x + 4, y + h - 10, kMuted, label);
    }
}

void drawSidePanel(const RuntimeView& v, int x, int y, int w, int h) {
    (void)w;
    (void)h;
    int row = y;
    const int step = 11;

    if (v.mode == Mode::Sweep) {
        text(x, row, kMuted, "noise floor");
        row += step;
        if (v.heardAnything) {
            textf(x, row, kInk, "%.0f dBm", v.lastRssiDbm);
        } else {
            text(x, row, kMuted, "--");
        }
        row += step + 4;
        textf(x, row, kMuted, "%.3f MHz", v.freqMhz);
        return;
    }

    text(x, row, kMuted, "last heard");
    row += step;
    if (!v.heardAnything) {
        text(x, row, kMuted, "nothing yet");
        row += step;
    } else {
        const SignalBand band = classifyRssi(v.lastRssiDbm);
        textf(x, row, signalStyle(band).rgb565, "%.0f dBm", v.lastRssiDbm);
        row += step;
        textf(x, row, kMuted, "snr %.1f", v.lastSnrDb);
        row += step;
        if (v.lastDistanceM > 0.0) {
            if (v.lastDistanceM >= 1000.0) {
                textf(x, row, kInk, "%.2f km", v.lastDistanceM / 1000.0);
            } else {
                textf(x, row, kInk, "%.0f m", v.lastDistanceM);
            }
            row += step;
        }
    }

    if (v.link != nullptr && v.link->totalReceived() > 0) {
        row += 3;
        textf(x, row, kMuted, "loss %.0f%%", v.link->overallLossRatio() * 100.0);
        row += step;
        const double far = v.link->farthestContactM();
        if (far > 0.0) {
            textf(x, row, kMuted, far >= 1000.0 ? "max %.1fkm" : "max %.0fm",
                  far >= 1000.0 ? far / 1000.0 : far);
        }
    }
}

}  // namespace

void uiBegin() {
    M5.Display.setRotation(1);
    M5.Display.setBrightness(120);
    g_canvas.setColorDepth(16);
    g_canvas.createSprite(kScreenWidth, kScreenHeight);
    g_canvas.setTextSize(1);
    g_canvas.setFont(&fonts::Font0);
}

void drawFatal(const char* title, const char* detail, const char* hint) {
    g_canvas.fillSprite(kBg);
    g_canvas.fillRect(0, 0, kScreenWidth, 18, kDanger);
    g_canvas.setTextColor(kBg);
    g_canvas.drawString(title, 5, 5);

    g_canvas.setTextColor(kInk);
    g_canvas.setTextWrap(true);
    g_canvas.drawString(detail, 5, 28);
    if (hint != nullptr) {
        g_canvas.setTextColor(kMuted);
        g_canvas.drawString(hint, 5, kScreenHeight - 22);
    }
    g_canvas.setTextWrap(false);
    g_canvas.pushSprite(0, 0);
}

void drawMenu(const char* title, const char* const* items, size_t count,
              size_t selected, const char* footer) {
    g_canvas.fillSprite(kBg);
    g_canvas.fillRect(0, 0, kScreenWidth, kHeaderH, kPanel);
    text(4, 3, kAccent, title);

    const int top = kHeaderH + 4;
    const int rowH = 14;
    const size_t visible = static_cast<size_t>((kScreenHeight - top - kFooterH) / rowH);
    // Keep the selection on screen without redrawing the whole list position
    // every frame.
    const size_t first = selected >= visible ? selected - visible + 1 : 0;

    for (size_t i = 0; i < count && i - first < visible; ++i) {
        if (i < first) continue;
        const int y = top + static_cast<int>(i - first) * rowH;
        if (i == selected) {
            g_canvas.fillRect(0, y - 1, kScreenWidth, rowH, kPanel);
            text(6, y + 2, kAccent, ">");
        }
        text(18, y + 2, i == selected ? kInk : kMuted, items[i]);
    }

    if (footer != nullptr) {
        g_canvas.fillRect(0, kScreenHeight - kFooterH, kScreenWidth, kFooterH, kPanel);
        text(4, kScreenHeight - kFooterH + 2, kMuted, footer);
    }
    g_canvas.pushSprite(0, 0);
}

void drawRunning(const RuntimeView& v) {
    g_canvas.fillSprite(kBg);
    drawHeader(v);

    const int bodyY = kHeaderH + 2;
    const int bodyH = kScreenHeight - kHeaderH - kFooterH - 4;
    const int trailW = 150;

    drawTrailPanel(v, 2, bodyY, trailW, bodyH);
    drawSidePanel(v, trailW + 8, bodyY + 2, kScreenWidth - trailW - 10, bodyH);

    drawFooter(v);
    g_canvas.pushSprite(0, 0);
}

void drawSummary(const SessionInfo& info, const LinkStats& link, const char* path) {
    g_canvas.fillSprite(kBg);
    g_canvas.fillRect(0, 0, kScreenWidth, kHeaderH, kPanel);
    text(4, 3, kAccent, "SESSION SAVED");

    int y = kHeaderH + 6;
    const int step = 11;

    textf(5, y, kInk, "%u pts  %u track", info.sweepSamples + info.packetSamples + info.linkSamples,
          info.trackPoints);
    y += step;

    if (link.totalReceived() > 0) {
        textf(5, y, kMuted, "loss %.1f%%  max %.0f m", link.overallLossRatio() * 100.0,
              link.farthestContactM());
        y += step;
    }
    if (info.txCount > 0) {
        textf(5, y, kMuted, "tx %u  airtime %.1f s", info.txCount, info.txAirtimeMs / 1000.0);
        y += step;
    }
    if (info.droppedSamples > 0) {
        textf(5, y, kWarn, "dropped %u (card too slow)", info.droppedSamples);
        y += step;
    }
    if (info.samplesWithoutFix > 0) {
        textf(5, y, kWarn, "%u without a usable fix", info.samplesWithoutFix);
        y += step;
    }

    y += 4;
    g_canvas.setTextWrap(true);
    text(5, y, kMuted, path);
    g_canvas.setTextWrap(false);

    g_canvas.fillRect(0, kScreenHeight - kFooterH, kScreenWidth, kFooterH, kPanel);
    text(4, kScreenHeight - kFooterH + 2, kMuted, "ENTER: menu");
    g_canvas.pushSprite(0, 0);
}

void drawBusy(const char* message) {
    g_canvas.fillSprite(kBg);
    text(8, kScreenHeight / 2 - 4, kInk, message);
    g_canvas.pushSprite(0, 0);
}

}  // namespace app
}  // namespace lorascout
