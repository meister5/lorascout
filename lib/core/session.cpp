#include "session.h"

#include <cstdio>
#include <cstring>

#include "exporters.h"

namespace lorascout {
namespace {

std::string quoted(const char* s) {
    return "\"" + jsonEscape(s == nullptr ? "" : s) + "\"";
}

std::string isoOrNull(const GnssTime& t) {
    char buf[32] = {};
    if (!t.toIso8601(buf, sizeof(buf))) return "null";
    return "\"" + std::string(buf) + "\"";
}

}  // namespace

const char* modeName(Mode m) {
    switch (m) {
        case Mode::Sweep: return "sweep";
        case Mode::Listen: return "listen";
        case Mode::Beacon: return "beacon";
        case Mode::Rover: return "rover";
        default: return "unknown";
    }
}

bool modeTransmits(Mode m) { return m == Mode::Beacon; }

std::string makeSessionId(const GnssTime& t, uint64_t uptimeMs) {
    char buf[32] = {};
    if (t.valid) {
        std::snprintf(buf, sizeof(buf), "%04u%02u%02uT%02u%02u%02uZ",
                      static_cast<unsigned>(t.year), static_cast<unsigned>(t.month),
                      static_cast<unsigned>(t.day), static_cast<unsigned>(t.hour),
                      static_cast<unsigned>(t.minute), static_cast<unsigned>(t.second));
    } else {
        std::snprintf(buf, sizeof(buf), "boot-%010llu",
                      static_cast<unsigned long long>(uptimeMs));
    }
    return buf;
}

std::string sessionDirName(const SessionInfo& s) {
    std::string id = s.id[0] ? std::string(s.id) : std::string("session");
    std::string mode = s.mode[0] ? std::string(s.mode) : std::string("unknown");
    return id + "-" + mode;
}

std::string sessionJson(const SessionInfo& s, const LinkStats& link) {
    const Region region = regionFromCode(s.regionCode);
    const RegionSpec& spec =
        regionSpec(region == Region::Count ? Region::EU868 : region);

    std::string out;
    out.reserve(2048);
    out += "{\n";
    out += "  \"firmware\": " + quoted(kFirmwareVersion) + ",\n";
    out += "  \"device\": \"M5Stack Cardputer ADV + Cap LoRa-1262\",\n";
    out += "  \"session_id\": " + quoted(s.id) + ",\n";
    out += "  \"mode\": " + quoted(s.mode) + ",\n";
    out += "  \"node_id\": " + std::to_string(s.nodeId) + ",\n";

    out += "  \"radio\": {\n";
    out += "    \"preset\": " + quoted(s.presetName) + ",\n";
    out += "    \"freq_mhz\": " + formatFixed(s.freqMhz, 4) + ",\n";
    out += "    \"bw_khz\": " + formatFixed(s.bwKhz, 1) + ",\n";
    out += "    \"sf\": " + std::to_string(s.sf) + ",\n";
    out += "    \"cr\": " + std::to_string(s.cr) + ",\n";
    out += "    \"hopping\": " + std::string(s.hopping ? "true" : "false") + "\n";
    out += "  },\n";

    // The compliance envelope that was actually applied. This is the part
    // somebody auditing a transmit log needs, so it is recorded whether or not
    // the session transmitted.
    out += "  \"compliance\": {\n";
    out += "    \"region\": " + quoted(s.regionCode) + ",\n";
    out += "    \"region_name\": " + quoted(spec.name) + ",\n";
    out += "    \"tx_power_dbm_conducted\": " + std::to_string(s.txDbm) + ",\n";
    out += "    \"antenna_gain_dbi\": " + formatFixed(s.antennaGainDbi, 1) + ",\n";
    out += "    \"max_eirp_dbm\": " + formatFixed(s.maxEirpDbm, 2) + ",\n";
    out += "    \"applied_eirp_dbm\": " +
           formatFixed(static_cast<double>(s.txDbm) + s.antennaGainDbi, 2) + ",\n";
    out += "    \"duty_cycle_fraction\": " + formatFixed(s.dutyFraction, 4) + ",\n";
    out += "    \"max_dwell_ms\": " + std::to_string(s.maxDwellMs) + ",\n";
    out += "    \"transmissions\": " + std::to_string(s.txCount) + ",\n";
    out += "    \"airtime_ms\": " + std::to_string(s.txAirtimeMs) + ",\n";
    out += "    \"note\": " + quoted(spec.note) + "\n";
    out += "  },\n";

    out += "  \"privacy\": {\n";
    out += "    \"payload_capture\": " +
           std::string(s.payloadCaptureEnabled ? "true" : "false") + ",\n";
    out += "    \"note\": \"Packet payloads are not retained unless payload "
           "capture was explicitly enabled; logs otherwise record only length, "
           "CRC status and a truncated hash.\"\n";
    out += "  },\n";

    out += "  \"started_utc\": " + isoOrNull(s.startUtc) + ",\n";
    out += "  \"ended_utc\": " + isoOrNull(s.endUtc) + ",\n";
    out += "  \"duration_ms\": " +
           std::to_string(s.endUptimeMs > s.startUptimeMs ? s.endUptimeMs - s.startUptimeMs : 0) +
           ",\n";

    out += "  \"counts\": {\n";
    out += "    \"sweep\": " + std::to_string(s.sweepSamples) + ",\n";
    out += "    \"packet\": " + std::to_string(s.packetSamples) + ",\n";
    out += "    \"link\": " + std::to_string(s.linkSamples) + ",\n";
    out += "    \"track\": " + std::to_string(s.trackPoints) + ",\n";
    out += "    \"samples_without_fix\": " + std::to_string(s.samplesWithoutFix) + "\n";
    out += "  },\n";

    out += "  \"health\": {\n";
    out += "    \"queue_high_water\": " + std::to_string(s.queueHighWater) + ",\n";
    out += "    \"dropped_samples\": " + std::to_string(s.droppedSamples) + "\n";
    out += "  },\n";

    out += "  \"link\": {\n";
    out += "    \"received\": " + std::to_string(link.totalReceived()) + ",\n";
    out += "    \"lost\": " + std::to_string(link.totalLost()) + ",\n";
    out += "    \"loss_ratio\": " + formatFixed(link.overallLossRatio(), 4) + ",\n";
    out += "    \"farthest_contact_m\": " + formatFixed(link.farthestContactM(), 1) + ",\n";
    out += "    \"nodes\": [";
    for (size_t i = 0; i < link.nodeCount(); ++i) {
        const LinkStats::Node& n = link.nodeAt(i);
        if (i > 0) out += ",";
        out += "\n      {\"node_id\": " + std::to_string(n.nodeId);
        out += ", \"received\": " + std::to_string(n.received);
        out += ", \"lost\": " + std::to_string(n.lost);
        out += ", \"duplicates\": " + std::to_string(n.duplicates);
        out += ", \"out_of_order\": " + std::to_string(n.outOfOrder);
        out += ", \"resyncs\": " + std::to_string(n.resyncs);
        out += ", \"loss_ratio\": " + formatFixed(n.lossRatio(), 4);
        out += ", \"best_rssi_dbm\": " + formatFixed(n.bestRssiDbm, 1);
        out += ", \"worst_rssi_dbm\": " + formatFixed(n.worstRssiDbm, 1);
        out += ", \"farthest_contact_m\": " + formatFixed(n.farthestContactM, 1);
        out += "}";
    }
    if (link.nodeCount() > 0) out += "\n    ";
    out += "]\n";
    out += "  }\n";
    out += "}\n";
    return out;
}

}  // namespace lorascout
