#include "app.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#include "../config.h"
#include "airtime.h"
#include "beacon.h"
#include "exporters.h"
#include "preset.h"

namespace lorascout {
namespace app {
namespace {

// millis() wraps after 49 days. Everything here is 64-bit from the start so
// that a long unattended beacon session cannot roll its duty-cycle window over.
uint64_t millis64() { return static_cast<uint64_t>(esp_timer_get_time() / 1000); }

portMUX_TYPE g_statusMux = portMUX_INITIALIZER_UNLOCKED;

const char* const kMenuItems[] = {
    "Sweep  - noise floor map",
    "Listen - passive packets",
    "Rover  - measure a beacon",
    "Beacon - transmit reference",
    "Settings",
};
constexpr size_t kMenuCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);

LoraParams loraParamsOf(const RadioConfig& c) {
    LoraParams p;
    p.spreadingFactor = c.sf;
    p.bandwidthKhz = c.bwKhz;
    p.codingRate = c.cr;
    p.preambleSymbols = c.preambleSymbols;
    return p;
}

void copyName(char* dst, size_t dstLen, const char* src) {
    if (dstLen == 0) return;
    std::snprintf(dst, dstLen, "%s", src == nullptr ? "" : src);
}

}  // namespace

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------

void App::begin() {
    auto cfg = M5.config();
    M5.begin(cfg);
    uiBegin();
    keys_.begin();
    settings_.load();

    if (!initHardware()) {
        screen_ = Screen::Fatal;
        drawCurrentScreen();
        return;
    }

    queue_ = xQueueCreate(kSampleQueueDepth, sizeof(QueuedSample));
    if (queue_ == nullptr) {
        fatalTitle_ = "OUT OF MEMORY";
        fatalDetail_ = "Could not allocate the sample queue.";
        fatalHint_ = nullptr;
        screen_ = Screen::Fatal;
        drawCurrentScreen();
        return;
    }

    // Pinned to core 0 so radio and GNSS servicing is never behind an SD flush
    // or a screen redraw.
    xTaskCreatePinnedToCore(&App::samplerTaskEntry, "sampler", 8192, this, 3,
                            &samplerHandle_, 0);

    screen_ = settings_.regionChosen() ? Screen::Menu : Screen::RegionPicker;
    recoverUnexportedSession();
    drawCurrentScreen();
}

bool App::initHardware() {
    gnss_.begin();

    if (!cap_.begin() || cap_.variant() == hal::CapVariant::None) {
        fatalTitle_ = "NO CAP DETECTED";
        fatalDetail_ = "lorascout needs a Cardputer ADV with the Cap LoRa-1262 "
                       "fitted to the rear Cap-Bus header.";
        fatalHint_ = "Check the cap is seated, then power cycle.";
        return false;
    }

    if (!radio_.begin()) {
        fatalTitle_ = "RADIO NOT FOUND";
        fatalDetail_ = "The SX1262 did not respond. On the Cap LoRa-1262 this "
                       "usually means the cap is not seated on the Cap-Bus.";
        fatalHint_ = radio_.lastError();
        return false;
    }

    // Antenna before power, always. Keying a PA into an unterminated port is
    // how a radio gets damaged, and on the -1262 the antenna path is behind an
    // IO expander that defaults to off.
    if (cap_.variant() == hal::CapVariant::LoRa1262 && !cap_.antennaPathEnabled()) {
        fatalTitle_ = "ANTENNA PATH OFF";
        fatalDetail_ = "The FM8625H antenna switch could not be enabled. "
                       "Transmitting now would drive the PA into an open port.";
        fatalHint_ = nullptr;
        return false;
    }

    storage_.begin();  // absence of a card is survivable; it is shown in the UI

    // The export index is large and cold, so it belongs in PSRAM. Without
    // PSRAM the device still surveys -- the CSVs are the real record -- it just
    // cannot build the derived maps on board.
    exportPoints_ = static_cast<ExportPoint*>(
        heap_caps_malloc(sizeof(ExportPoint) * kMaxExportPoints, MALLOC_CAP_SPIRAM));
    return true;
}

// ---------------------------------------------------------------------------
// compliance
// ---------------------------------------------------------------------------

bool App::applyCompliance(Mode mode, double freqMhz) {
    const ChannelVerdict v = checkChannel(settings_.region, freqMhz);
    if (!v.allowed) {
        complianceReason_ = v.reason;
        return false;
    }

    maxEirpDbm_ = v.maxEirpDbm;
    dutyFraction_ = v.dutyFraction;
    maxDwellMs_ = v.maxDwellMs;
    requiresLbt_ = v.requiresLbt;
    txDbm_ = maxConductedDbm(v.maxEirpDbm, settings_.antennaGainDbi);
    duty_.configure(v.dutyFraction, v.maxDwellMs);

    const RegionSpec& spec = regionSpec(settings_.region);
    // Where a stationary single-channel transmitter is not lawful, hopping is
    // not a preference.
    hopping_ = modeTransmits(mode) ? (settings_.hopping || spec.requiresHopping)
                                   : settings_.hopping;

    complianceReason_ = "OK";
    return true;
}

// ---------------------------------------------------------------------------
// session lifecycle
// ---------------------------------------------------------------------------

bool App::startSession(Mode mode) {
    if (!settings_.regionChosen()) {
        screen_ = Screen::RegionPicker;
        return false;
    }

    mode_ = mode;
    const RegionSpec& spec = regionSpec(settings_.region);

    // Choose the radio configuration for the mode.
    RadioConfig cfg;
    const RadioPreset* preset = nullptr;
    switch (mode) {
        case Mode::Sweep:
            // Receive-only, so any legal channel will do; the sweep retunes as
            // it goes.
            preset = &presetAt(0);
            cfg.freqMhz = spec.defaultListenMhz;
            break;
        case Mode::Listen:
            listenPreset_ = settings_.listenPresetIndex % presetCount();
            preset = &presetAt(listenPreset_);
            cfg.freqMhz = presetFrequency(*preset, settings_.region);
            break;
        case Mode::Beacon:
        case Mode::Rover:
            preset = settings_.beaconSf >= 10 ? &beaconLongRangePreset() : &beaconPreset();
            cfg.freqMhz = spec.defaultBeaconMhz;
            break;
        default:
            return false;
    }

    cfg.bwKhz = preset->bwKhz;
    cfg.sf = (mode == Mode::Beacon || mode == Mode::Rover) ? settings_.beaconSf : preset->sf;
    cfg.cr = preset->cr;
    cfg.syncWord = preset->syncWord;
    cfg.preambleSymbols = preset->preambleSymbols;
    copyName(cfg.presetName, sizeof(cfg.presetName), preset->name);

    if (!applyCompliance(mode, cfg.freqMhz)) {
        fatalTitle_ = "CHANNEL NOT USABLE";
        fatalDetail_ = complianceReason_;
        fatalHint_ = "Change region in Settings.";
        screen_ = Screen::Fatal;
        return false;
    }

    // Transmit is only ever permitted on lorascout's own sync word. This is the
    // last gate before the PA, and it is unconditional.
    if (modeTransmits(mode) && !preset->transmitAllowed) {
        fatalTitle_ = "TRANSMIT REFUSED";
        fatalDetail_ = "Beacon mode may only transmit on the lorascout preset, "
                       "never on another network's parameters.";
        fatalHint_ = nullptr;
        screen_ = Screen::Fatal;
        return false;
    }

    // A frame we could never legally send is caught before the session starts
    // rather than at the first transmission.
    if (modeTransmits(mode)) {
        const uint32_t airtime = airtimeMs(loraParamsOf(cfg), kBeaconFrameSize);
        const DutyCycleTracker::Decision d = duty_.evaluate(millis64(), airtime);
        if (!d.allowed && d.waitMs == 0) {
            fatalTitle_ = "FRAME TOO LONG";
            fatalDetail_ = d.reason;
            fatalHint_ = "Lower the beacon spreading factor in Settings.";
            screen_ = Screen::Fatal;
            return false;
        }
    }

    // Build the sweep channel plan across the legal band this module can reach.
    sweepChannelCount_ = 0;
    if (mode == Mode::Sweep) {
        const double lo = spec.bandLoMhz > kModuleFreqMinMhz ? spec.bandLoMhz : kModuleFreqMinMhz;
        const double hi = spec.bandHiMhz < kModuleFreqMaxMhz ? spec.bandHiMhz : kModuleFreqMaxMhz;
        for (double f = lo; f <= hi && sweepChannelCount_ < kMaxSweepChannels;
             f += kSweepStepMhz) {
            if (checkChannel(settings_.region, f).allowed) {
                sweepChannels_[sweepChannelCount_++] = f;
            }
        }
        if (sweepChannelCount_ == 0) {
            fatalTitle_ = "NO CHANNELS";
            fatalDetail_ = "No frequency in this region falls within the "
                           "module's 868-923 MHz range.";
            fatalHint_ = nullptr;
            screen_ = Screen::Fatal;
            return false;
        }
        sweepIndex_ = 0;
    }

    // Session bookkeeping.
    info_ = SessionInfo{};
    copyName(info_.mode, sizeof(info_.mode), modeName(mode));
    copyName(info_.regionCode, sizeof(info_.regionCode), spec.code);
    copyName(info_.presetName, sizeof(info_.presetName), preset->name);
    info_.nodeId = settings_.effectiveNodeId();
    info_.freqMhz = cfg.freqMhz;
    info_.bwKhz = cfg.bwKhz;
    info_.sf = cfg.sf;
    info_.cr = cfg.cr;
    info_.txDbm = modeTransmits(mode) ? txDbm_ : 0;
    info_.antennaGainDbi = settings_.antennaGainDbi;
    info_.maxEirpDbm = maxEirpDbm_;
    info_.dutyFraction = dutyFraction_;
    info_.maxDwellMs = maxDwellMs_;
    info_.hopping = hopping_;
    info_.payloadCaptureEnabled = settings_.payloadCapture;
    info_.startUtc = gnss_.fix().time;
    info_.startUptimeMs = millis64();

    const std::string id = makeSessionId(info_.startUtc, info_.startUptimeMs);
    copyName(info_.id, sizeof(info_.id), id.c_str());

    link_.reset();
    trail_.clear();
    duty_.reset();
    duty_.configure(dutyFraction_, maxDwellMs_);
    exportCount_ = 0;
    exportTruncated_ = false;
    droppedSamples_ = 0;
    lastDistanceM_ = 0.0;
    lastSnrDb_ = 0.0f;
    beaconSeq_ = 0;
    lastBeaconMs_ = 0;
    lastTrackMs_ = 0;
    lastPresetSwitchMs_ = millis64();

    if (storage_.available()) storage_.openSession(sessionDirName(info_));

    radioConfig_ = cfg;
    sessionRunning_ = true;
    samplerRunning_ = true;
    screen_ = Screen::Running;
    return true;
}

void App::stopSession() {
    samplerRunning_ = false;
    // Let the sampler finish whatever it is mid-way through before the radio is
    // reconfigured out from under it.
    vTaskDelay(pdMS_TO_TICKS(60));
    radio_.standby();

    drainQueue();

    info_.endUtc = gnss_.fix().time;
    info_.endUptimeMs = millis64();
    info_.droppedSamples = droppedSamples_;

    if (storage_.sessionOpen()) {
        drawBusy("writing exports...");
        storage_.flushNow();
        writeDerivedExports();
        storage_.closeSession();
    }

    sessionRunning_ = false;
    screen_ = Screen::Summary;
}

bool App::writeDerivedExports() {
    if (!storage_.sessionOpen()) return false;

    // session.json first: if power is lost during the maps, the provenance and
    // the transmit log still survive.
    storage_.writeFile("session.json", sessionJson(info_, link_));

    if (exportPoints_ == nullptr) return false;

    std::string metadata = "{\"session_id\": \"" + std::string(info_.id) +
                           "\", \"mode\": \"" + std::string(info_.mode) +
                           "\", \"region\": \"" + std::string(info_.regionCode) +
                           "\", \"points\": " + std::to_string(exportCount_) +
                           ", \"truncated\": " + (exportTruncated_ ? "true" : "false") + "}";

    std::string geo = geoJsonHeader(metadata);
    std::string kmlOut = kmlHeader(std::string(info_.id) + " " + info_.mode);
    std::string gpxOut = gpxHeader(std::string(info_.id));
    bool first = true;

    for (size_t i = 0; i < exportCount_; ++i) {
        const ExportPoint& e = exportPoints_[i];

        MapPoint mp;
        mp.coord = e.coord;
        mp.altitudeM = e.altitudeM;
        mp.valueDbm = e.valueDbm;
        mp.band = static_cast<SignalBand>(e.band);
        mp.title = formatFixed(e.valueDbm, 0) + " dBm";
        // Times are stored as epoch seconds on device and rendered back to
        // ISO-8601 here, which keeps each index entry small.
        if (e.unixSeconds != 0) {
            const time_t t = static_cast<time_t>(e.unixSeconds);
            struct tm g;
            gmtime_r(&t, &g);
            char iso[32] = {};
            strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", &g);
            mp.timeIso = iso;
        }

        if (e.kind == static_cast<uint8_t>(QueuedSample::Kind::Track)) {
            TrackPoint tp;
            tp.coord = e.coord;
            tp.altitudeM = e.altitudeM;
            tp.timeIso = mp.timeIso;
            gpxOut += gpxPoint(tp);
        } else {
            const std::string feature = geoJsonFeature(mp, first);
            if (!feature.empty()) {
                geo += feature;
                first = false;
                kmlOut += kmlPlacemark(mp);
            }
        }

        // Flush in chunks so a long session never needs the whole document in
        // RAM at once.
        if (geo.size() > 16384) {
            storage_.writeFile("points.geojson.part", geo);
            geo.clear();
        }
    }

    geo += geoJsonFooter();
    kmlOut += kmlFooter();
    gpxOut += gpxFooter();

    storage_.writeFile("points.geojson", geo);
    storage_.writeFile("points.kml", kmlOut);
    storage_.writeFile("track.gpx", gpxOut);
    return true;
}

void App::recoverUnexportedSession() {
    if (!storage_.available()) return;
    const std::string orphan = storage_.findUnexportedSession();
    if (orphan.empty()) return;
    // Logs with no maps is what a flat battery leaves behind. The CSVs are
    // intact by design, so the session is not lost -- but say so on screen
    // rather than letting the user assume the maps are there.
    std::snprintf(recoveryNote_, sizeof(recoveryNote_),
                  "%s has logs but no maps", orphan.c_str());
}

// ---------------------------------------------------------------------------
// writer side (core 1)
// ---------------------------------------------------------------------------

void App::drainQueue() {
    if (queue_ == nullptr) return;
    QueuedSample s;
    // Bounded per pass so the UI keeps redrawing even under a heavy burst.
    int budget = 32;
    while (budget-- > 0 && xQueueReceive(queue_, &s, 0) == pdTRUE) {
        recordSample(s);
    }
    storage_.flushIfDue(millis64());
}

void App::recordSample(const QueuedSample& s) {
    const GeoStamp* geo = nullptr;
    float value = 0.0f;
    SignalBand band = SignalBand::None;

    switch (s.kind) {
        case QueuedSample::Kind::Sweep:
            geo = &s.sweep.geo;
            value = s.sweep.rssiMean;
            band = SignalBand::None;
            ++info_.sweepSamples;
            storage_.appendSweep(sweepCsvRow(s.sweep), sweepCsvHeader());
            break;

        case QueuedSample::Kind::Packet:
            geo = &s.link.packet.geo;
            value = s.link.packet.rssiDbm;
            band = classifyRssi(value);
            ++info_.packetSamples;
            storage_.appendPacket(packetCsvRow(s.link.packet), packetCsvHeader());
            break;

        case QueuedSample::Kind::Link:
            geo = &s.link.packet.geo;
            value = s.link.packet.rssiDbm;
            band = classifyRssi(value);
            ++info_.linkSamples;
            link_.observe(s.link.nodeId, s.link.seq, s.link.packet.rssiDbm,
                          s.link.packet.snrDb, s.link.distanceM, millis64());
            lastDistanceM_ = s.link.distanceM;
            lastSnrDb_ = s.link.packet.snrDb;
            storage_.appendLink(linkCsvRow(s.link), linkCsvHeader());
            break;

        case QueuedSample::Kind::Track:
            geo = &s.track;
            ++info_.trackPoints;
            storage_.appendTrack(trackCsvRow(s.track), trackCsvHeader());
            break;
    }

    if (geo == nullptr) return;
    if (!geo->surveyGrade()) ++info_.samplesWithoutFix;

    if (s.kind != QueuedSample::Kind::Track) {
        trail_.add(geo->coord, band, value);
    }

    if (exportPoints_ != nullptr && coordValid(geo->coord)) {
        if (exportCount_ < kMaxExportPoints) {
            ExportPoint& e = exportPoints_[exportCount_++];
            e.coord = geo->coord;
            e.altitudeM = geo->altitudeM;
            e.valueDbm = value;
            e.unixSeconds = static_cast<uint32_t>(geo->utc.toUnixSeconds());
            e.band = static_cast<uint8_t>(band);
            e.kind = static_cast<uint8_t>(s.kind);
        } else {
            exportTruncated_ = true;
        }
    }
}

// ---------------------------------------------------------------------------
// sampler side (core 0)
// ---------------------------------------------------------------------------

void App::samplerTaskEntry(void* arg) { static_cast<App*>(arg)->samplerTask(); }

GeoStamp App::stamp(uint64_t nowMs) const {
    return GeoStamp::from(gnss_.fix(), nowMs);
}

bool App::push(const QueuedSample& s) {
    if (queue_ == nullptr) return false;
    if (xQueueSend(queue_, &s, 0) != pdTRUE) {
        // The card could not keep up. Dropping is correct -- blocking here
        // would cost packets, not just records -- but it must be counted and
        // reported, because it means the survey has holes that are the
        // firmware's fault rather than the radio's.
        ++droppedSamples_;
        return false;
    }
    return true;
}

void App::samplerTask() {
    bool configured = false;

    for (;;) {
        const uint64_t now = millis64();
        gnss_.poll(now);

        if (!samplerRunning_) {
            configured = false;
            publishStatus();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!configured) {
            radio_.applyConfig(radioConfig_);
            radio_.setTxPower(txDbm_);
            if (mode_ != Mode::Beacon) radio_.startReceive();
            configured = true;
        }

        switch (mode_) {
            case Mode::Sweep: sampleSweep(now); break;
            case Mode::Listen: sampleListen(now); break;
            case Mode::Beacon: sampleBeacon(now); break;
            case Mode::Rover: sampleRover(now); break;
            default: break;
        }

        emitTrackIfDue(now);
        publishStatus();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void App::sampleSweep(uint64_t nowMs) {
    if (sweepChannelCount_ == 0) return;

    const double freq = sweepChannels_[sweepIndex_];
    RadioConfig cfg = radioConfig_;
    cfg.freqMhz = freq;
    radio_.applyConfig(cfg);
    radio_.startReceive();
    vTaskDelay(pdMS_TO_TICKS(5));   // let the PLL settle before believing the AGC

    float minR = 0.0f;
    float maxR = -200.0f;
    float sum = 0.0f;
    for (uint16_t i = 0; i < kSweepReadsPerChannel; ++i) {
        const float r = radio_.readRssi();
        if (i == 0 || r < minR) minR = r;
        if (r > maxR) maxR = r;
        sum += r;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    QueuedSample s;
    s.kind = QueuedSample::Kind::Sweep;
    s.sweep.geo = stamp(nowMs);
    s.sweep.freqMhz = freq;
    s.sweep.rssiMin = minR;
    s.sweep.rssiMean = sum / kSweepReadsPerChannel;
    s.sweep.rssiMax = maxR;
    s.sweep.reads = kSweepReadsPerChannel;
    s.sweep.channelBusy = radio_.channelBusy();
    push(s);

    sweepIndex_ = (sweepIndex_ + 1) % sweepChannelCount_;
}

void App::sampleListen(uint64_t nowMs) {
    // Rotate presets on a dwell timer. LoRa cannot be scanned, so coverage of
    // several presets is bought with time on each -- and the cost is that any
    // one network is only heard for part of the survey. The dwell is recorded
    // per sample so an export can be filtered by preset afterwards.
    if (settings_.rotatePresets && nowMs - lastPresetSwitchMs_ > kPresetDwellMs) {
        do {
            listenPreset_ = (listenPreset_ + 1) % presetCount();
        } while (presetAt(listenPreset_).transmitAllowed);   // skip our own

        const RadioPreset& p = presetAt(listenPreset_);
        RadioConfig cfg;
        cfg.freqMhz = presetFrequency(p, settings_.region);
        cfg.bwKhz = p.bwKhz;
        cfg.sf = p.sf;
        cfg.cr = p.cr;
        cfg.syncWord = p.syncWord;
        cfg.preambleSymbols = p.preambleSymbols;
        copyName(cfg.presetName, sizeof(cfg.presetName), p.name);

        if (checkChannel(settings_.region, cfg.freqMhz).allowed) {
            radioConfig_ = cfg;
            radio_.applyConfig(cfg);
            radio_.startReceive();
        }
        lastPresetSwitchMs_ = nowMs;
    }

    if (!radio_.packetPending()) return;

    hal::RxPacket rx;
    if (!radio_.readPacket(&rx)) return;

    QueuedSample s;
    s.kind = QueuedSample::Kind::Packet;
    s.link.packet.geo = stamp(nowMs);
    s.link.packet.radio = radioConfig_;
    s.link.packet.rssiDbm = rx.rssiDbm;
    s.link.packet.snrDb = rx.snrDb;
    s.link.packet.freqErrorHz = rx.freqErrorHz;
    s.link.packet.lengthBytes = static_cast<uint16_t>(rx.length);
    s.link.packet.crcOk = rx.crcOk;
    // Only a hash is retained. Recording that a frame with these radio
    // parameters arrived here at this strength is the whole measurement; the
    // contents are somebody else's traffic and are not ours to keep.
    s.link.packet.payloadHash = payloadHash(rx.data, rx.length);
    push(s);
}

void App::sampleBeacon(uint64_t nowMs) {
    const LoraParams params = loraParamsOf(radioConfig_);
    const uint32_t airtime = airtimeMs(params, kBeaconFrameSize);
    const uint32_t interval = duty_.minIntervalMs(airtime);

    if (lastBeaconMs_ != 0 && nowMs - lastBeaconMs_ < interval) return;

    const DutyCycleTracker::Decision decision = duty_.evaluate(nowMs, airtime);
    if (!decision.allowed) return;

    const GnssFix& fix = gnss_.fix();

    // Follow the hop schedule. Both ends derive the channel from GNSS time, the
    // only clock they share without ever having been in contact.
    if (hopping_) {
        const uint64_t unix = fix.time.toUnixSeconds();
        const double freq = hopChannelMhz(settings_.region, unix, kHopPeriodSeconds);
        if (freq != radioConfig_.freqMhz) {
            const ChannelVerdict v = checkChannel(settings_.region, freq);
            if (!v.allowed) return;   // never transmit on an unverified channel
            radioConfig_.freqMhz = freq;
            radio_.applyConfig(radioConfig_);
            radio_.setTxPower(txDbm_);
        }
    }

    // Listen before talk, where the region requires it.
    if (requiresLbt_ && radio_.channelBusy()) return;

    BeaconFrame frame;
    frame.nodeId = settings_.effectiveNodeId();
    frame.seq = beaconSeq_;
    frame.coord = fix.coord;
    frame.altitudeM = static_cast<int16_t>(fix.altitudeM);
    frame.txDbm = static_cast<int8_t>(txDbm_);
    frame.flags = 0;
    if (fix.fixType == FixType::Fix3D) frame.flags |= kBeaconFlagFix3D;
    if (!fix.usableForSurvey()) frame.flags |= kBeaconFlagPositionStale;
    if (hopping_) frame.flags |= kBeaconFlagHopping;

    uint8_t buf[kBeaconFrameSize] = {};
    if (encodeBeacon(frame, buf, sizeof(buf)) != kBeaconFrameSize) return;

    if (!radio_.transmit(buf, kBeaconFrameSize)) return;

    duty_.record(nowMs, airtime);
    lastBeaconMs_ = nowMs;
    ++beaconSeq_;
    ++info_.txCount;
    info_.txAirtimeMs += airtime;
}

void App::sampleRover(uint64_t nowMs) {
    if (hopping_) {
        const uint64_t unix = gnss_.fix().time.toUnixSeconds();
        // Without GNSS time there is no shared schedule, so the receiver parks
        // on the first channel and hears roughly one hop in N. The UI says so.
        const double freq = hopChannelMhz(settings_.region, unix, kHopPeriodSeconds);
        if (freq != radioConfig_.freqMhz && checkChannel(settings_.region, freq).allowed) {
            radioConfig_.freqMhz = freq;
            radio_.applyConfig(radioConfig_);
            radio_.startReceive();
        }
    }

    if (!radio_.packetPending()) return;

    hal::RxPacket rx;
    if (!radio_.readPacket(&rx)) return;

    QueuedSample s;
    s.link.packet.geo = stamp(nowMs);
    s.link.packet.radio = radioConfig_;
    s.link.packet.rssiDbm = rx.rssiDbm;
    s.link.packet.snrDb = rx.snrDb;
    s.link.packet.freqErrorHz = rx.freqErrorHz;
    s.link.packet.lengthBytes = static_cast<uint16_t>(rx.length);
    s.link.packet.crcOk = rx.crcOk;
    s.link.packet.payloadHash = payloadHash(rx.data, rx.length);

    BeaconFrame frame;
    if (rx.crcOk && decodeBeacon(rx.data, rx.length, &frame)) {
        s.kind = QueuedSample::Kind::Link;
        s.link.nodeId = frame.nodeId;
        s.link.seq = frame.seq;
        s.link.beaconCoord = frame.coord;
        s.link.beaconAltM = frame.altitudeM;
        s.link.beaconTxDbm = frame.txDbm;
        s.link.beaconFixValid = frame.fix3D() && !frame.positionStale();
        computeLinkGeometry(s.link);
    } else {
        // Something else entirely landed on our parameters. Still a real
        // measurement, just not one with a known transmitter.
        s.kind = QueuedSample::Kind::Packet;
    }
    push(s);
}

void App::emitTrackIfDue(uint64_t nowMs) {
    if (nowMs - lastTrackMs_ < kTrackIntervalMs) return;
    lastTrackMs_ = nowMs;

    const GeoStamp g = stamp(nowMs);
    if (!g.surveyGrade()) return;

    QueuedSample s;
    s.kind = QueuedSample::Kind::Track;
    s.track = g;
    push(s);
    // The file write happens on the writer side. This task must never touch
    // storage: an SD flush blocks for tens of milliseconds and a LoRa packet
    // arrives when it arrives.
}

void App::publishStatus() {
    SamplerStatus s;
    s.fix = gnss_.fix();
    s.fixAgeMs = gnss_.fixAgeMs(millis64());
    s.freqMhz = radioConfig_.freqMhz;
    s.sf = radioConfig_.sf;
    s.txDbm = txDbm_;
    s.txCount = info_.txCount;
    s.txAirtimeMs = info_.txAirtimeMs;
    s.dropped = droppedSamples_;
    s.radioOk = true;
    s.queueDepth = queue_ ? static_cast<uint32_t>(uxQueueMessagesWaiting(queue_)) : 0;

    if (modeTransmits(mode_)) {
        const uint32_t airtime = airtimeMs(loraParamsOf(radioConfig_), kBeaconFrameSize);
        const uint64_t now = millis64();
        s.dutyUtilization = duty_.utilization(now);
        s.minIntervalMs = duty_.minIntervalMs(airtime);
        const DutyCycleTracker::Decision d = duty_.evaluate(now, airtime);
        s.dutyBlocked = !d.allowed;
        s.nextTxInMs = d.waitMs;
    }

    portENTER_CRITICAL(&g_statusMux);
    status_ = s;
    portEXIT_CRITICAL(&g_statusMux);
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void App::loop() {
    if (screen_ == Screen::Fatal) {
        // Nothing here is recoverable by carrying on, so the screen stays put.
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }

    const hal::Key key = keys_.poll();
    if (key != hal::Key::None) handleKey(key);

    if (sessionRunning_) drainQueue();

    const uint64_t now = millis64();
    if (now - lastUiMs_ >= 100) {
        lastUiMs_ = now;
        drawCurrentScreen();
    }
    vTaskDelay(pdMS_TO_TICKS(5));
}

void App::drawCurrentScreen() {
    switch (screen_) {
        case Screen::Fatal:
            drawFatal(fatalTitle_ ? fatalTitle_ : "ERROR",
                      fatalDetail_ ? fatalDetail_ : "", fatalHint_);
            return;

        case Screen::RegionPicker: {
            static char labels[regionCount()][48];
            static const char* items[regionCount()];
            for (size_t i = 0; i < regionCount(); ++i) {
                const RegionSpec& spec = regionAt(i);
                std::snprintf(labels[i], sizeof(labels[i]), "%-6s %s", spec.code,
                              spec.moduleSupported ? spec.name : "(hardware cannot)");
                items[i] = labels[i];
            }
            drawMenu("SELECT REGION", items, regionCount(), regionIndex_,
                     "ENTER: choose   sets legal limits");
            return;
        }

        case Screen::Menu: {
            char footer[80];
            if (recoveryNote_[0] != '\0') {
                std::snprintf(footer, sizeof(footer), "%s", recoveryNote_);
            } else {
                std::snprintf(footer, sizeof(footer), "%s  %s  node %u",
                              regionCode(settings_.region), cap_.variantName(),
                              settings_.effectiveNodeId());
            }
            drawMenu("LORASCOUT", kMenuItems, kMenuCount, menuIndex_, footer);
            return;
        }

        case Screen::Settings_: {
            static char labels[6][48];
            static const char* items[6];
            std::snprintf(labels[0], sizeof(labels[0]), "Region: %s",
                          regionCode(settings_.region));
            std::snprintf(labels[1], sizeof(labels[1]), "Antenna gain: %.1f dBi",
                          settings_.antennaGainDbi);
            std::snprintf(labels[2], sizeof(labels[2]), "Beacon SF: %u",
                          settings_.beaconSf);
            std::snprintf(labels[3], sizeof(labels[3]), "Rotate presets: %s",
                          settings_.rotatePresets ? "on" : "off");
            std::snprintf(labels[4], sizeof(labels[4]), "Channel hopping: %s",
                          settings_.hopping ? "on" : "off");
            std::snprintf(labels[5], sizeof(labels[5]), "Keep payloads: %s",
                          settings_.payloadCapture ? "ON (logs others' data)" : "off");
            for (size_t i = 0; i < 6; ++i) items[i] = labels[i];
            drawMenu("SETTINGS", items, 6, settingsIndex_,
                     "LEFT/RIGHT: change   ESC: back");
            return;
        }

        case Screen::Running: {
            SamplerStatus s;
            portENTER_CRITICAL(&g_statusMux);
            s = status_;
            portEXIT_CRITICAL(&g_statusMux);

            RuntimeView v;
            v.mode = mode_;
            v.regionCode = regionCode(settings_.region);
            v.capName = cap_.variantName();
            v.presetName = info_.presetName;
            v.fix = &s.fix;
            v.fixAgeMs = s.fixAgeMs;
            v.trail = &trail_;
            v.link = &link_;
            v.samples = info_.sweepSamples + info_.packetSamples + info_.linkSamples;
            v.dropped = s.dropped;
            v.queueDepth = s.queueDepth;
            v.freqMhz = s.freqMhz;
            v.sf = s.sf;
            v.txDbm = s.txDbm;
            v.txCount = s.txCount;
            v.dutyUtilization = s.dutyUtilization;
            v.minIntervalMs = s.minIntervalMs;
            v.nextTxInMs = s.nextTxInMs;
            v.dutyBlocked = s.dutyBlocked;
            v.sdOk = storage_.available();
            v.batteryPercent = M5.Power.getBatteryLevel();

            if (!trail_.empty()) {
                v.heardAnything = true;
                v.lastRssiDbm = trail_.newest().valueDbm;
            }
            v.lastSnrDb = lastSnrDb_;
            v.lastDistanceM = lastDistanceM_;
            drawRunning(v);
            return;
        }

        case Screen::Summary:
            drawSummary(info_, link_, storage_.sessionPath().c_str());
            return;
    }
}

void App::handleKey(hal::Key key) {
    switch (screen_) {
        case Screen::RegionPicker: handleRegionKey(key); break;
        case Screen::Menu: handleMenuKey(key); break;
        case Screen::Settings_: handleSettingsKey(key); break;
        case Screen::Running: handleRunningKey(key); break;
        case Screen::Summary:
            if (key == hal::Key::Enter || key == hal::Key::Back) screen_ = Screen::Menu;
            break;
        default: break;
    }
}

void App::handleRegionKey(hal::Key key) {
    if (key == hal::Key::Up && regionIndex_ > 0) --regionIndex_;
    if (key == hal::Key::Down && regionIndex_ + 1 < regionCount()) ++regionIndex_;
    if (key != hal::Key::Enter) return;

    const RegionSpec& spec = regionAt(regionIndex_);
    if (!spec.moduleSupported) return;   // refuse rather than pretend

    settings_.region = spec.id;
    settings_.hopping = spec.requiresHopping;
    settings_.save();
    screen_ = Screen::Menu;
}

void App::handleMenuKey(hal::Key key) {
    if (key == hal::Key::Up && menuIndex_ > 0) --menuIndex_;
    if (key == hal::Key::Down && menuIndex_ + 1 < kMenuCount) ++menuIndex_;
    if (key != hal::Key::Enter) return;

    switch (menuIndex_) {
        case 0: startSession(Mode::Sweep); break;
        case 1: startSession(Mode::Listen); break;
        case 2: startSession(Mode::Rover); break;
        case 3: startSession(Mode::Beacon); break;
        case 4: screen_ = Screen::Settings_; settingsIndex_ = 0; break;
        default: break;
    }
}

void App::handleSettingsKey(hal::Key key) {
    if (key == hal::Key::Up && settingsIndex_ > 0) --settingsIndex_;
    if (key == hal::Key::Down && settingsIndex_ < 5) ++settingsIndex_;
    if (key == hal::Key::Back) {
        settings_.save();
        screen_ = Screen::Menu;
        return;
    }

    const int delta = key == hal::Key::Right ? 1 : key == hal::Key::Left ? -1 : 0;
    if (delta == 0) return;

    switch (settingsIndex_) {
        case 0:
            screen_ = Screen::RegionPicker;
            break;
        case 1:
            settings_.antennaGainDbi += delta * 0.5;
            if (settings_.antennaGainDbi < 0.0) settings_.antennaGainDbi = 0.0;
            if (settings_.antennaGainDbi > 12.0) settings_.antennaGainDbi = 12.0;
            break;
        case 2: {
            int sf = static_cast<int>(settings_.beaconSf) + delta;
            if (sf < 7) sf = 7;
            if (sf > 12) sf = 12;
            settings_.beaconSf = static_cast<uint8_t>(sf);
            break;
        }
        case 3: settings_.rotatePresets = !settings_.rotatePresets; break;
        case 4:
            // Where the regulator requires hopping it is not a preference.
            if (!regionSpec(settings_.region).requiresHopping) {
                settings_.hopping = !settings_.hopping;
            }
            break;
        case 5: settings_.payloadCapture = !settings_.payloadCapture; break;
        default: break;
    }
    settings_.save();
}

void App::handleRunningKey(hal::Key key) {
    if (key == hal::Key::Back || key == hal::Key::Enter) stopSession();
}

}  // namespace app
}  // namespace lorascout
