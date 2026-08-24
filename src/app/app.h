// The application: mode state machine, the sampler/writer split, and session
// lifecycle.
//
// Concurrency, in full:
//   core 0, sampler task -- owns the radio and the GNSS receiver, does the
//     duty-cycle accounting, and pushes fixed-size records into a queue. It
//     never touches the filesystem, because a microSD flush blocks for tens of
//     milliseconds and a LoRa packet arrives when it arrives.
//   core 1, loop() -- drains the queue, appends to the CSVs, updates the trail
//     and statistics, draws the UI and handles keys.
// Nothing else is shared. The queue is thread-safe on its own, and the small
// status snapshot the UI reads is guarded by a spinlock.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstdint>

#include "../hal/cap.h"
#include "../hal/gnss.h"
#include "../hal/keys.h"
#include "../hal/radio.h"
#include "../hal/storage.h"
#include "dutycycle.h"
#include "linkstats.h"
#include "sample.h"
#include "session.h"
#include "settings.h"
#include "trail.h"
#include "ui.h"

namespace lorascout {
namespace app {

// Frequency step used by the noise-floor sweep, and the cap on how many
// channels one pass covers. A full pass has to finish fast enough that the
// device has not meaningfully moved between the first channel and the last.
constexpr double kSweepStepMhz = 0.2;
constexpr size_t kMaxSweepChannels = 48;
constexpr uint16_t kSweepReadsPerChannel = 8;

// How long listen mode dwells on one preset before rotating. LoRa cannot be
// scanned, so coverage of several presets is bought with time on each.
constexpr uint32_t kPresetDwellMs = 20000;

// Hop dwell shared by beacon and rover, in seconds of GNSS time.
constexpr uint32_t kHopPeriodSeconds = 4;

constexpr uint32_t kTrackIntervalMs = 1000;

// Derived exports are built from an in-memory index of the session. Beyond this
// many points the maps are truncated -- and the truncation is reported, on
// screen and in session.json, never silently.
constexpr size_t kMaxExportPoints = 20000;

struct QueuedSample {
    enum class Kind : uint8_t { Sweep, Packet, Link, Track };
    Kind kind = Kind::Track;
    SweepSample sweep;
    LinkSample link;    // packet-only records use .packet and leave nodeId at 0
    GeoStamp track;
};

// Snapshot the UI reads. POD, copied under a spinlock.
struct SamplerStatus {
    GnssFix fix;
    uint32_t fixAgeMs = 0;
    double freqMhz = 0.0;
    uint8_t sf = 7;
    int txDbm = 0;
    uint32_t txCount = 0;
    uint32_t txAirtimeMs = 0;
    double dutyUtilization = 0.0;
    uint32_t minIntervalMs = 0;
    uint32_t nextTxInMs = 0;
    bool dutyBlocked = false;
    uint32_t dropped = 0;
    uint32_t queueDepth = 0;
    bool radioOk = false;
};

class App {
public:
    void begin();
    void loop();

private:
    enum class Screen : uint8_t { Fatal, RegionPicker, Menu, Settings_, Running, Summary };

    // --- setup ---
    bool initHardware();
    void publishStatus();

    // --- screens ---
    void drawCurrentScreen();
    void handleKey(hal::Key key);
    void handleMenuKey(hal::Key key);
    void handleRegionKey(hal::Key key);
    void handleSettingsKey(hal::Key key);
    void handleRunningKey(hal::Key key);

    // --- session lifecycle ---
    bool startSession(Mode mode);
    void stopSession();
    void drainQueue();
    void recordSample(const QueuedSample& s);
    bool writeDerivedExports();
    void recoverUnexportedSession();

    // --- compliance ---
    // Resolves region + frequency into an actually-applied transmit envelope.
    // Returns false when the requested channel is not usable, with the reason
    // left in complianceReason_.
    bool applyCompliance(Mode mode, double freqMhz);

    // --- sampler ---
    static void samplerTaskEntry(void* arg);
    void samplerTask();
    void sampleSweep(uint64_t nowMs);
    void sampleListen(uint64_t nowMs);
    void sampleBeacon(uint64_t nowMs);
    void sampleRover(uint64_t nowMs);
    void emitTrackIfDue(uint64_t nowMs);
    bool push(const QueuedSample& s);
    GeoStamp stamp(uint64_t nowMs) const;

    // hardware
    hal::Cap cap_;
    hal::Radio radio_;
    hal::Gnss gnss_;
    hal::Storage storage_;
    hal::Keys keys_;

    // state
    Settings settings_;
    Screen screen_ = Screen::Menu;
    size_t menuIndex_ = 0;
    size_t regionIndex_ = 0;
    size_t settingsIndex_ = 0;
    const char* fatalTitle_ = nullptr;
    const char* fatalDetail_ = nullptr;
    const char* fatalHint_ = nullptr;
    const char* complianceReason_ = "";

    // session
    Mode mode_ = Mode::Sweep;
    SessionInfo info_;
    LinkStats link_;
    Trail trail_;
    bool sessionRunning_ = false;
    uint64_t lastUiMs_ = 0;
    double lastDistanceM_ = 0.0;
    float lastSnrDb_ = 0.0f;
    char recoveryNote_[64] = {};

    // derived-export index, in PSRAM when available
    struct ExportPoint {
        Coord coord;
        float altitudeM;
        float valueDbm;
        uint32_t unixSeconds;
        uint8_t band;
        uint8_t kind;      // QueuedSample::Kind
    };
    ExportPoint* exportPoints_ = nullptr;
    size_t exportCount_ = 0;
    bool exportTruncated_ = false;

    // sampler-owned
    RadioConfig radioConfig_;
    DutyCycleTracker duty_;
    int txDbm_ = 0;
    double maxEirpDbm_ = 0.0;
    uint32_t maxDwellMs_ = 0;
    bool requiresLbt_ = false;
    double dutyFraction_ = 0.0;
    bool hopping_ = false;
    uint16_t beaconSeq_ = 0;
    uint64_t lastBeaconMs_ = 0;
    uint64_t lastTrackMs_ = 0;
    uint64_t lastPresetSwitchMs_ = 0;
    size_t listenPreset_ = 0;
    double sweepChannels_[kMaxSweepChannels] = {};
    size_t sweepChannelCount_ = 0;
    size_t sweepIndex_ = 0;

    volatile bool samplerRunning_ = false;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t samplerHandle_ = nullptr;
    SamplerStatus status_;
    uint32_t droppedSamples_ = 0;
};

}  // namespace app
}  // namespace lorascout
