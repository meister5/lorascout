#include "../support/check.h"
#include "exporters.h"
#include "session.h"

#include <cstring>
#include <limits>

using namespace lorascout;

namespace {

// Counts CSV columns while respecting quoting, so a preset name containing a
// comma cannot silently shift every column to its right.
size_t countColumns(const std::string& line) {
    size_t cols = 1;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"') { ++i; continue; }
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            ++cols;
        } else if (c == '\n') {
            break;
        }
    }
    return cols;
}

GeoStamp sampleStamp() {
    GeoStamp g;
    g.uptimeMs = 123456;
    g.utc.year = 2026; g.utc.month = 8; g.utc.day = 24;
    g.utc.hour = 13; g.utc.minute = 45; g.utc.second = 2;
    g.utc.centisecond = 30; g.utc.valid = true;
    g.coord = Coord::fromDegrees(51.5004, -0.1246);
    g.altitudeM = 45.2f;
    g.speedKph = 4.5f;
    g.courseDeg = 148.1f;
    g.hdop = 0.94f;
    g.meanCn0 = 33.6f;
    g.satsUsed = 9;
    g.satsInView = 11;
    g.fixType = FixType::Fix3D;
    g.fixValid = true;
    return g;
}

}  // namespace

int main() {
    // --- escaping ---------------------------------------------------------
    CHECK_STREQ(csvEscape("plain"), "plain");
    CHECK_STREQ(csvEscape("has,comma"), "\"has,comma\"");
    CHECK_STREQ(csvEscape("say \"hi\""), "\"say \"\"hi\"\"\"");
    CHECK_STREQ(csvEscape("line\nbreak"), "\"line\nbreak\"");
    CHECK_STREQ(jsonEscape("a\"b\\c"), "a\\\"b\\\\c");
    CHECK_STREQ(jsonEscape("tab\there"), "tab\\there");
    CHECK_CONTAINS(jsonEscape(std::string("\x01")), "\\u0001");
    CHECK_STREQ(xmlEscape("a<b>&c\"d'"), "a&lt;b&gt;&amp;c&quot;d&apos;");

    CHECK_STREQ(formatFixed(1.23456, 2), "1.23");
    CHECK_STREQ(coordToString(515004000), "51.5004000");
    CHECK_STREQ(coordToString(-1246000), "-0.1246000");
    // Non-finite values must never reach a file as "nan" or "inf".
    CHECK_STREQ(formatFixed(std::numeric_limits<double>::quiet_NaN(), 2), "");
    CHECK_STREQ(formatFixed(std::numeric_limits<double>::infinity(), 2), "");

    // --- CSV shape --------------------------------------------------------
    SweepSample sweep;
    sweep.geo = sampleStamp();
    sweep.freqMhz = 868.1;
    sweep.rssiMin = -121.5f;
    sweep.rssiMean = -118.2f;
    sweep.rssiMax = -110.0f;
    sweep.reads = 16;
    sweep.channelBusy = true;
    CHECK_EQ(countColumns(sweepCsvHeader()), countColumns(sweepCsvRow(sweep)));
    CHECK_CONTAINS(sweepCsvRow(sweep), "51.5004000");
    CHECK_CONTAINS(sweepCsvRow(sweep), "2026-08-24T13:45:02.30Z");

    PacketSample pkt;
    pkt.geo = sampleStamp();
    pkt.radio.freqMhz = 869.525;
    pkt.radio.bwKhz = 250.0f;
    pkt.radio.sf = 11;
    pkt.radio.cr = 5;
    pkt.radio.syncWord = 0x2B;
    std::strcpy(pkt.radio.presetName, "Meshtastic LongFast");
    pkt.rssiDbm = -103.5f;
    pkt.snrDb = -7.25f;
    pkt.freqErrorHz = 412.0f;
    pkt.lengthBytes = 42;
    pkt.crcOk = true;
    pkt.payloadHash = 0xDEADBEEF;
    CHECK_EQ(countColumns(packetCsvHeader()), countColumns(packetCsvRow(pkt)));
    CHECK_CONTAINS(packetCsvRow(pkt), "deadbeef");
    CHECK_CONTAINS(packetCsvRow(pkt), "0x2B");
    CHECK_CONTAINS(packetCsvRow(pkt), "fair");   // -103.5 dBm

    // A preset name containing a comma must not break the column count.
    PacketSample tricky = pkt;
    std::strcpy(tricky.radio.presetName, "odd,name");
    CHECK_EQ(countColumns(packetCsvHeader()), countColumns(packetCsvRow(tricky)));

    LinkSample link;
    link.packet = pkt;
    link.nodeId = 4242;
    link.seq = 77;
    link.beaconCoord = Coord::fromDegrees(51.5104, -0.1146);
    link.beaconAltM = 60;
    link.beaconTxDbm = 13;
    link.beaconFixValid = true;
    computeLinkGeometry(link);
    CHECK_EQ(countColumns(linkCsvHeader()), countColumns(linkCsvRow(link)));

    // Link geometry: ~1.2 km north-east, and a measured loss well above free
    // space, which is the whole point of the measurement.
    // 0.01 deg north (1112 m) and 0.01 deg east at 51.5 lat (692 m).
    CHECK_NEAR(link.distanceM, 1310.0, 5.0);
    CHECK_TRUE(link.bearingDeg > 20.0 && link.bearingDeg < 60.0);
    CHECK_NEAR(link.measuredPathLossDb, 13.0 - (-103.5), 0.1);
    CHECK_TRUE(link.freeSpacePathLossDb > 80.0 && link.freeSpacePathLossDb < 100.0);
    CHECK_NEAR(link.excessLossDb, link.measuredPathLossDb - link.freeSpacePathLossDb, 0.01);

    // Without a beacon fix, loss is still reported but distance is not invented.
    LinkSample noFix = link;
    noFix.beaconFixValid = false;
    computeLinkGeometry(noFix);
    CHECK_NEAR(noFix.distanceM, 0.0, 1e-9);
    CHECK_NEAR(noFix.excessLossDb, 0.0, 1e-9);
    CHECK_NEAR(noFix.measuredPathLossDb, 116.5, 0.1);

    // --- GeoJSON ----------------------------------------------------------
    std::vector<MapPoint> points;
    points.push_back(mapPointOf(pkt));
    points.push_back(mapPointOf(sweep));
    points.push_back(mapPointOf(link));

    // A sample taken with no fix must not become a point on a map.
    PacketSample unfixed = pkt;
    unfixed.geo.coord = Coord{0, 0};
    points.push_back(mapPointOf(unfixed));

    const std::string gj = geoJson(points, "{\"session\": \"test\"}");
    CHECK_CONTAINS(gj, "\"type\": \"FeatureCollection\"");
    CHECK_CONTAINS(gj, "\"marker-color\": \"#f1c40f\"");
    CHECK_CONTAINS(gj, "\"session\": \"test\"");
    CHECK_CONTAINS(gj, "\"excess_loss_db\"");
    // GeoJSON is lon,lat -- getting this backwards puts every survey in the
    // wrong hemisphere, so it is worth asserting explicitly.
    CHECK_CONTAINS(gj, "[-0.1246000, 51.5004000");
    // Three valid points, so two separators.
    size_t features = 0;
    for (size_t i = gj.find("\"type\": \"Feature\""); i != std::string::npos;
         i = gj.find("\"type\": \"Feature\"", i + 1)) {
        ++features;
    }
    CHECK_EQ(features, 3u);
    // A noise-floor reading is not a received signal and must not be coloured
    // as one.
    CHECK_CONTAINS(gj, "\"kind\": \"sweep\"");
    CHECK_CONTAINS(gj, "\"band\": \"none\"");

    // Empty input is still valid JSON, not a truncated file.
    const std::string empty = geoJson({}, "");
    CHECK_CONTAINS(empty, "\"features\": [");
    CHECK_CONTAINS(empty, "\"metadata\": {}");

    // --- KML --------------------------------------------------------------
    const std::string k = kml(points, "survey & test");
    CHECK_CONTAINS(k, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    CHECK_CONTAINS(k, "survey &amp; test");
    CHECK_CONTAINS(k, "<styleUrl>#fair</styleUrl>");
    CHECK_CONTAINS(k, "<when>2026-08-24T13:45:02.30Z</when>");
    // KML orders coordinates lon,lat too.
    CHECK_CONTAINS(k, "<coordinates>-0.1246000,51.5004000");

    // --- GPX --------------------------------------------------------------
    std::vector<TrackPoint> track;
    TrackPoint tp;
    tp.coord = Coord::fromDegrees(51.5004, -0.1246);
    tp.altitudeM = 45.2;
    tp.timeIso = "2026-08-24T13:45:02.30Z";
    track.push_back(tp);
    TrackPoint bad;  // no fix
    track.push_back(bad);

    const std::string g = gpx(track, "walk");
    CHECK_CONTAINS(g, "<gpx version=\"1.1\"");
    CHECK_CONTAINS(g, "lat=\"51.5004000\" lon=\"-0.1246000\"");
    CHECK_CONTAINS(g, "<ele>45.2</ele>");
    size_t trkpts = 0;
    for (size_t i = g.find("<trkpt"); i != std::string::npos; i = g.find("<trkpt", i + 1)) ++trkpts;
    CHECK_EQ(trkpts, 1u);

    // --- session.json -----------------------------------------------------
    SessionInfo info;
    std::strcpy(info.id, "20260824T134502Z");
    std::strcpy(info.mode, "rover");
    std::strcpy(info.regionCode, "EU868");
    std::strcpy(info.presetName, "lorascout survey");
    info.nodeId = 4242;
    info.freqMhz = 868.1;
    info.txDbm = 13;
    info.antennaGainDbi = 3.0;
    info.maxEirpDbm = 16.15;
    info.dutyFraction = 0.01;
    info.txCount = 120;
    info.txAirtimeMs = 6240;
    info.startUtc = sampleStamp().utc;
    info.startUptimeMs = 1000;
    info.endUptimeMs = 61000;
    info.linkSamples = 118;
    info.droppedSamples = 0;

    LinkStats stats;
    for (uint16_t s = 1; s <= 5; ++s) stats.observe(4242, s, -101.0f, -3.0f, 900.0, s * 1000);
    stats.observe(4242, 8, -104.0f, -6.0f, 1100.0, 9000);

    const std::string js = sessionJson(info, stats);
    CHECK_CONTAINS(js, "\"session_id\": \"20260824T134502Z\"");
    CHECK_CONTAINS(js, "\"region\": \"EU868\"");
    // The applied EIRP must be recorded, and must sit at or below the ceiling.
    CHECK_CONTAINS(js, "\"applied_eirp_dbm\": 16.00");
    CHECK_CONTAINS(js, "\"max_eirp_dbm\": 16.15");
    CHECK_CONTAINS(js, "\"airtime_ms\": 6240");
    CHECK_CONTAINS(js, "\"payload_capture\": false");
    CHECK_CONTAINS(js, "\"duration_ms\": 60000");
    CHECK_CONTAINS(js, "\"node_id\": 4242");
    CHECK_CONTAINS(js, "\"lost\": 2");

    CHECK_STREQ(sessionDirName(info), "20260824T134502Z-rover");

    // A session must be nameable before the first fix, or a cold start indoors
    // has nowhere to write.
    GnssTime noTime;
    CHECK_STREQ(makeSessionId(noTime, 12345), "boot-0000012345");
    CHECK_STREQ(makeSessionId(sampleStamp().utc, 0), "20260824T134502Z");

    // --- streaming exports match the batch form ---------------------------
    // The device writes these a feature at a time; the two paths must agree
    // byte for byte or the on-card file diverges from what the tests cover.
    {
        std::string streamed = geoJsonHeader("{\"session\": \"test\"}");
        bool first = true;
        for (const MapPoint& mp : points) {
            const std::string feature = geoJsonFeature(mp, first);
            if (feature.empty()) continue;
            streamed += feature;
            first = false;
        }
        streamed += geoJsonFooter();
        CHECK_STREQ(streamed, gj);

        std::string streamedKml = kmlHeader("survey & test");
        for (const MapPoint& mp : points) streamedKml += kmlPlacemark(mp);
        streamedKml += kmlFooter();
        CHECK_STREQ(streamedKml, k);

        std::string streamedGpx = gpxHeader("walk");
        for (const TrackPoint& tpt : track) streamedGpx += gpxPoint(tpt);
        streamedGpx += gpxFooter();
        CHECK_STREQ(streamedGpx, g);
    }

    // A point with no fix yields nothing at all, so it can never consume the
    // separating comma and produce invalid JSON.
    {
        MapPoint nowhere;
        nowhere.coord = Coord{0, 0};
        CHECK_STREQ(geoJsonFeature(nowhere, true), "");
        CHECK_STREQ(geoJsonFeature(nowhere, false), "");
        CHECK_STREQ(kmlPlacemark(nowhere), "");
        TrackPoint nowhereTrack;
        CHECK_STREQ(gpxPoint(nowhereTrack), "");

        // Leading invalid point: the first real feature must still be emitted
        // without a leading comma.
        std::vector<MapPoint> leadingBad;
        leadingBad.push_back(nowhere);
        leadingBad.push_back(mapPointOf(pkt));
        const std::string out = geoJson(leadingBad, "");
        CHECK_TRUE(out.find("[\n    {") != std::string::npos);
        CHECK_TRUE(out.find(",\n    {") == std::string::npos);
    }

    CHECK_STREQ(modeName(Mode::Sweep), "sweep");
    CHECK_TRUE(modeTransmits(Mode::Beacon));
    CHECK_FALSE(modeTransmits(Mode::Listen));
    CHECK_FALSE(modeTransmits(Mode::Sweep));
    CHECK_FALSE(modeTransmits(Mode::Rover));

    return check::finish("exporters");
}
