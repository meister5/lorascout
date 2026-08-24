#include "exporters.h"

#include <cmath>
#include <cstdio>

namespace lorascout {
namespace {

std::string u(uint64_t v) { return std::to_string(v); }
std::string i(long long v) { return std::to_string(v); }

// Non-finite values would produce "nan" or "inf", which no CSV reader and no
// JSON parser will accept. Empty is the honest representation of "not measured".
std::string num(double v, int decimals) {
    if (!std::isfinite(v)) return "";
    return formatFixed(v, decimals);
}

std::string geoStampCsv(const GeoStamp& g) {
    char iso[32] = {};
    g.utc.toIso8601(iso, sizeof(iso));
    std::string out;
    out += u(g.uptimeMs);
    out += ',';
    out += iso;
    out += ',';
    out += coordToString(g.coord.lat_e7);
    out += ',';
    out += coordToString(g.coord.lon_e7);
    out += ',';
    out += num(g.altitudeM, 1);
    out += ',';
    out += num(g.speedKph, 2);
    out += ',';
    out += num(g.courseDeg, 1);
    out += ',';
    out += fixTypeName(g.fixType);
    out += ',';
    out += i(g.satsUsed);
    out += ',';
    out += i(g.satsInView);
    out += ',';
    out += num(g.hdop, 2);
    out += ',';
    out += num(g.meanCn0, 1);
    out += ',';
    out += (g.surveyGrade() ? "1" : "0");
    return out;
}

constexpr const char* kGeoStampHeader =
    "uptime_ms,utc,lat,lon,alt_m,speed_kph,course_deg,fix_type,sats_used,"
    "sats_in_view,hdop,mean_cn0,survey_grade";

std::string radioCsv(const RadioConfig& r) {
    std::string out;
    out += num(r.freqMhz, 4);
    out += ',';
    out += num(r.bwKhz, 1);
    out += ',';
    out += i(r.sf);
    out += ',';
    out += i(r.cr);
    out += ',';
    char sync[8] = {};
    std::snprintf(sync, sizeof(sync), "0x%02X", r.syncWord);
    out += sync;
    out += ',';
    out += csvEscape(r.presetName);
    return out;
}

constexpr const char* kRadioHeader = "freq_mhz,bw_khz,sf,cr,sync_word,preset";

std::string geoStampProps(const GeoStamp& g) {
    std::string p;
    p += "\"alt_m\": " + num(g.altitudeM, 1);
    p += ", \"speed_kph\": " + num(g.speedKph, 2);
    p += ", \"fix_type\": \"" + std::string(fixTypeName(g.fixType)) + "\"";
    p += ", \"sats_used\": " + i(g.satsUsed);
    p += ", \"hdop\": " + num(g.hdop, 2);
    p += ", \"survey_grade\": " + std::string(g.surveyGrade() ? "true" : "false");
    return p;
}

}  // namespace

std::string formatFixed(double value, int decimals) {
    if (!std::isfinite(value)) return "";
    char buf[48] = {};
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return buf;
}

std::string coordToString(int32_t e7) {
    // Seven decimals exactly: the storage resolution, no more and no less, so a
    // round trip through the file is lossless.
    return formatFixed(static_cast<double>(e7) / 1e7, 7);
}

const char* fixTypeName(FixType t) {
    switch (t) {
        case FixType::Fix2D: return "2D";
        case FixType::Fix3D: return "3D";
        case FixType::None:
        default: return "none";
    }
}

std::string csvEscape(const std::string& field) {
    bool needsQuotes = false;
    for (char c : field) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return field;

    std::string out = "\"";
    for (char c : field) {
        if (c == '"') out += "\"\"";
        else if (c == '\r') continue;
        else out += c;
    }
    out += '"';
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8] = {};
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string sweepCsvHeader() {
    return std::string(kGeoStampHeader) +
           ",freq_mhz,rssi_min_dbm,rssi_mean_dbm,rssi_max_dbm,reads,channel_busy\n";
}

std::string sweepCsvRow(const SweepSample& s) {
    std::string out = geoStampCsv(s.geo);
    out += ',';
    out += num(s.freqMhz, 4);
    out += ',';
    out += num(s.rssiMin, 1);
    out += ',';
    out += num(s.rssiMean, 1);
    out += ',';
    out += num(s.rssiMax, 1);
    out += ',';
    out += i(s.reads);
    out += ',';
    out += (s.channelBusy ? "1" : "0");
    out += '\n';
    return out;
}

std::string packetCsvHeader() {
    return std::string(kGeoStampHeader) + "," + kRadioHeader +
           ",rssi_dbm,snr_db,freq_error_hz,length_bytes,crc_ok,payload_hash,band\n";
}

std::string packetCsvRow(const PacketSample& s) {
    std::string out = geoStampCsv(s.geo);
    out += ',';
    out += radioCsv(s.radio);
    out += ',';
    out += num(s.rssiDbm, 1);
    out += ',';
    out += num(s.snrDb, 2);
    out += ',';
    out += num(s.freqErrorHz, 0);
    out += ',';
    out += i(s.lengthBytes);
    out += ',';
    out += (s.crcOk ? "1" : "0");
    out += ',';
    char hash[16] = {};
    std::snprintf(hash, sizeof(hash), "%08x", s.payloadHash);
    out += hash;
    out += ',';
    out += signalStyle(classifyRssi(s.rssiDbm)).label;
    out += '\n';
    return out;
}

std::string linkCsvHeader() {
    return std::string(kGeoStampHeader) + "," + kRadioHeader +
           ",rssi_dbm,snr_db,freq_error_hz,length_bytes,crc_ok,band,"
           "node_id,seq,beacon_lat,beacon_lon,beacon_alt_m,beacon_tx_dbm,"
           "distance_m,bearing_deg,path_loss_db,fspl_db,excess_loss_db\n";
}

std::string linkCsvRow(const LinkSample& s) {
    std::string out = geoStampCsv(s.packet.geo);
    out += ',';
    out += radioCsv(s.packet.radio);
    out += ',';
    out += num(s.packet.rssiDbm, 1);
    out += ',';
    out += num(s.packet.snrDb, 2);
    out += ',';
    out += num(s.packet.freqErrorHz, 0);
    out += ',';
    out += i(s.packet.lengthBytes);
    out += ',';
    out += (s.packet.crcOk ? "1" : "0");
    out += ',';
    out += signalStyle(classifyRssi(s.packet.rssiDbm)).label;
    out += ',';
    out += i(s.nodeId);
    out += ',';
    out += i(s.seq);
    out += ',';
    out += coordToString(s.beaconCoord.lat_e7);
    out += ',';
    out += coordToString(s.beaconCoord.lon_e7);
    out += ',';
    out += i(s.beaconAltM);
    out += ',';
    out += i(s.beaconTxDbm);
    out += ',';
    out += num(s.distanceM, 1);
    out += ',';
    out += num(s.bearingDeg, 1);
    out += ',';
    out += num(s.measuredPathLossDb, 1);
    out += ',';
    out += num(s.freeSpacePathLossDb, 1);
    out += ',';
    out += num(s.excessLossDb, 1);
    out += '\n';
    return out;
}

std::string trackCsvHeader() { return std::string(kGeoStampHeader) + "\n"; }

std::string trackCsvRow(const GeoStamp& g) { return geoStampCsv(g) + "\n"; }

MapPoint mapPointOf(const SweepSample& s) {
    MapPoint p;
    p.coord = s.geo.coord;
    p.altitudeM = s.geo.altitudeM;
    p.valueDbm = s.rssiMean;
    // A noise floor is not a received signal, so it gets no signal band: a
    // quiet channel and a strong packet must never share a colour.
    p.band = SignalBand::None;
    char iso[32] = {};
    s.geo.utc.toIso8601(iso, sizeof(iso));
    p.timeIso = iso;
    p.title = "noise " + formatFixed(s.freqMhz, 3) + " MHz";
    p.extraProperties = "\"kind\": \"sweep\", \"freq_mhz\": " + formatFixed(s.freqMhz, 4) +
                        ", \"rssi_mean_dbm\": " + formatFixed(s.rssiMean, 1) +
                        ", \"rssi_min_dbm\": " + formatFixed(s.rssiMin, 1) +
                        ", \"rssi_max_dbm\": " + formatFixed(s.rssiMax, 1) +
                        ", \"channel_busy\": " + (s.channelBusy ? "true" : "false") +
                        ", " + geoStampProps(s.geo);
    return p;
}

MapPoint mapPointOf(const PacketSample& s) {
    MapPoint p;
    p.coord = s.geo.coord;
    p.altitudeM = s.geo.altitudeM;
    p.valueDbm = s.rssiDbm;
    p.band = classifyRssi(s.rssiDbm);
    char iso[32] = {};
    s.geo.utc.toIso8601(iso, sizeof(iso));
    p.timeIso = iso;
    p.title = formatFixed(s.rssiDbm, 0) + " dBm";
    p.extraProperties = "\"kind\": \"packet\", \"rssi_dbm\": " + formatFixed(s.rssiDbm, 1) +
                        ", \"snr_db\": " + formatFixed(s.snrDb, 2) +
                        ", \"freq_mhz\": " + formatFixed(s.radio.freqMhz, 4) +
                        ", \"sf\": " + std::to_string(s.radio.sf) +
                        ", \"preset\": \"" + jsonEscape(s.radio.presetName) + "\"" +
                        ", \"length_bytes\": " + std::to_string(s.lengthBytes) +
                        ", \"crc_ok\": " + (s.crcOk ? "true" : "false") +
                        ", " + geoStampProps(s.geo);
    return p;
}

MapPoint mapPointOf(const LinkSample& s) {
    MapPoint p = mapPointOf(s.packet);
    p.title = formatFixed(s.packet.rssiDbm, 0) + " dBm @ " +
              formatFixed(s.distanceM, 0) + " m";
    p.extraProperties += ", \"kind_detail\": \"link\", \"node_id\": " + std::to_string(s.nodeId) +
                         ", \"seq\": " + std::to_string(s.seq) +
                         ", \"distance_m\": " + formatFixed(s.distanceM, 1) +
                         ", \"bearing_deg\": " + formatFixed(s.bearingDeg, 1) +
                         ", \"path_loss_db\": " + formatFixed(s.measuredPathLossDb, 1) +
                         ", \"fspl_db\": " + formatFixed(s.freeSpacePathLossDb, 1) +
                         ", \"excess_loss_db\": " + formatFixed(s.excessLossDb, 1);
    return p;
}

std::string geoJsonHeader(const std::string& metadataJson) {
    std::string out = "{\n  \"type\": \"FeatureCollection\",\n";
    out += "  \"metadata\": " + (metadataJson.empty() ? std::string("{}") : metadataJson) + ",\n";
    out += "  \"features\": [\n";
    return out;
}

std::string geoJsonFeature(const MapPoint& p, bool first) {
    if (!coordValid(p.coord)) return "";

    const SignalStyle& style = signalStyle(p.band);
    std::string out;
    out.reserve(360);
    if (!first) out += ",\n";
    out += "    {\"type\": \"Feature\", \"geometry\": {\"type\": \"Point\", \"coordinates\": [";
    out += coordToString(p.coord.lon_e7);
    out += ", ";
    out += coordToString(p.coord.lat_e7);
    out += ", ";
    out += formatFixed(p.altitudeM, 1);
    out += "]}, \"properties\": {";
    out += "\"time\": \"" + jsonEscape(p.timeIso) + "\"";
    out += ", \"title\": \"" + jsonEscape(p.title) + "\"";
    out += ", \"band\": \"" + std::string(style.label) + "\"";
    // geojson.io and several other viewers honour simplestyle-spec, so the
    // exported map arrives already colour-coded.
    out += ", \"marker-color\": \"" + std::string(style.cssHex) + "\"";
    out += ", \"marker-size\": \"small\"";
    if (!p.extraProperties.empty()) out += ", " + p.extraProperties;
    out += "}}";
    return out;
}

std::string geoJsonFooter() { return "\n  ]\n}\n"; }

std::string geoJson(const std::vector<MapPoint>& points, const std::string& metadataJson) {
    std::string out = geoJsonHeader(metadataJson);
    out.reserve(points.size() * 320 + 256);
    bool first = true;
    for (const MapPoint& p : points) {
        const std::string feature = geoJsonFeature(p, first);
        if (feature.empty()) continue;   // skipped points must not consume the comma
        out += feature;
        first = false;
    }
    out += geoJsonFooter();
    return out;
}

std::string kmlHeader(const std::string& documentName) {
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n";
    out += "  <name>" + xmlEscape(documentName) + "</name>\n";
    for (size_t b = 0; b < static_cast<size_t>(SignalBand::Count); ++b) {
        const SignalStyle& style = signalStyle(static_cast<SignalBand>(b));
        out += "  <Style id=\"" + std::string(style.label) + "\">\n";
        out += "    <IconStyle><color>" + std::string(style.kmlAbgr) +
               "</color><scale>0.7</scale></IconStyle>\n";
        out += "    <LabelStyle><scale>0</scale></LabelStyle>\n";
        out += "  </Style>\n";
    }
    return out;
}

std::string kmlPlacemark(const MapPoint& p) {
    if (!coordValid(p.coord)) return "";
    const SignalStyle& style = signalStyle(p.band);
    std::string out;
    out += "  <Placemark>\n";
    out += "    <name>" + xmlEscape(p.title) + "</name>\n";
    out += "    <styleUrl>#" + std::string(style.label) + "</styleUrl>\n";
    if (!p.timeIso.empty()) {
        out += "    <TimeStamp><when>" + xmlEscape(p.timeIso) + "</when></TimeStamp>\n";
    }
    out += "    <Point><coordinates>";
    out += coordToString(p.coord.lon_e7);
    out += ",";
    out += coordToString(p.coord.lat_e7);
    out += ",";
    out += formatFixed(p.altitudeM, 1);
    out += "</coordinates></Point>\n";
    out += "  </Placemark>\n";
    return out;
}

std::string kmlFooter() { return "</Document>\n</kml>\n"; }

std::string kml(const std::vector<MapPoint>& points, const std::string& documentName) {
    std::string out = kmlHeader(documentName);
    out.reserve(points.size() * 300 + 1024);
    for (const MapPoint& p : points) out += kmlPlacemark(p);
    out += kmlFooter();
    return out;
}

std::string gpxHeader(const std::string& trackName) {
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<gpx version=\"1.1\" creator=\"lorascout\" "
           "xmlns=\"http://www.topografix.com/GPX/1/1\">\n";
    out += "  <trk>\n    <name>" + xmlEscape(trackName) + "</name>\n    <trkseg>\n";
    return out;
}

std::string gpxPoint(const TrackPoint& p) {
    if (!coordValid(p.coord)) return "";
    std::string out;
    out += "      <trkpt lat=\"" + coordToString(p.coord.lat_e7) +
           "\" lon=\"" + coordToString(p.coord.lon_e7) + "\">";
    out += "<ele>" + formatFixed(p.altitudeM, 1) + "</ele>";
    if (!p.timeIso.empty()) out += "<time>" + xmlEscape(p.timeIso) + "</time>";
    out += "</trkpt>\n";
    return out;
}

std::string gpxFooter() { return "    </trkseg>\n  </trk>\n</gpx>\n"; }

std::string gpx(const std::vector<TrackPoint>& points, const std::string& trackName) {
    std::string out = gpxHeader(trackName);
    out.reserve(points.size() * 160 + 512);
    for (const TrackPoint& p : points) out += gpxPoint(p);
    out += gpxFooter();
    return out;
}

}  // namespace lorascout
