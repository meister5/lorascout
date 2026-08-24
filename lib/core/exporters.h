// Serialisers for everything the device writes to the microSD card.
//
// The CSVs are the canonical log: append-only, one line per sample, flushed as
// they go. GeoJSON and KML are *derived* from them at session close. That split
// is deliberate -- a GeoJSON file left unterminated by a flat battery is
// unparseable, whereas a truncated CSV loses exactly one line. It also makes
// re-export free, so a session that lost power can be recovered on next boot.
#pragma once

#include <string>
#include <vector>

#include "linkstats.h"
#include "sample.h"
#include "signalband.h"

namespace lorascout {

// --- escaping -------------------------------------------------------------

std::string csvEscape(const std::string& field);
std::string jsonEscape(const std::string& s);
std::string xmlEscape(const std::string& s);

// --- canonical CSV --------------------------------------------------------

std::string sweepCsvHeader();
std::string sweepCsvRow(const SweepSample& s);

std::string packetCsvHeader();
std::string packetCsvRow(const PacketSample& s);

std::string linkCsvHeader();
std::string linkCsvRow(const LinkSample& s);

// The GNSS track gets a canonical CSV of its own for the same reason the sample
// logs do: track.gpx is derived at session close, and a derived file is worth
// nothing if the battery dies before it is written.
std::string trackCsvHeader();
std::string trackCsvRow(const GeoStamp& g);

// --- derived map exports --------------------------------------------------

// The common shape every sample collapses to for mapping purposes.
struct MapPoint {
    Coord coord;
    double altitudeM = 0.0;
    double valueDbm = 0.0;      // RSSI, or noise floor for sweeps
    SignalBand band = SignalBand::None;
    std::string timeIso;
    std::string title;
    // Already-formatted `"key": value` pairs, comma separated, no outer braces.
    std::string extraProperties;
};

struct TrackPoint {
    Coord coord;
    double altitudeM = 0.0;
    double speedKph = 0.0;
    std::string timeIso;
};

MapPoint mapPointOf(const SweepSample& s);
MapPoint mapPointOf(const PacketSample& s);
MapPoint mapPointOf(const LinkSample& s);

// Streaming form. A long survey holds more points than the device has RAM to
// serialise in one string, so exports are written a feature at a time straight
// to the card. The vector forms below are these same functions with a loop
// around them, and are what the host tests exercise.
//
// Points with an invalid coordinate produce an empty string and are skipped: an
// export is a map, and a point at (0,0) is a lie on a map. Because of that,
// callers must only advance their "first feature" flag when a non-empty string
// comes back.
std::string geoJsonHeader(const std::string& metadataJson);
std::string geoJsonFeature(const MapPoint& p, bool first);
std::string geoJsonFooter();

std::string kmlHeader(const std::string& documentName);
std::string kmlPlacemark(const MapPoint& p);
std::string kmlFooter();

std::string gpxHeader(const std::string& trackName);
std::string gpxPoint(const TrackPoint& p);
std::string gpxFooter();

// metadataJson is inserted as the collection's "metadata" member; pass "{}" or
// an empty string for none.
std::string geoJson(const std::vector<MapPoint>& points, const std::string& metadataJson);
std::string kml(const std::vector<MapPoint>& points, const std::string& documentName);
std::string gpx(const std::vector<TrackPoint>& points, const std::string& trackName);

// --- helpers --------------------------------------------------------------

std::string formatFixed(double value, int decimals);
std::string coordToString(int32_t e7);
const char* fixTypeName(FixType t);

}  // namespace lorascout
