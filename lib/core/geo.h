// Geodesy and link-budget math. No hardware, no Arduino: this header must stay
// compilable on the host so the native tests can exercise it.
#pragma once

#include <cstdint>

namespace lorascout {

// Mean Earth radius (IUGG). Haversine on a sphere is good to ~0.5% which is far
// below the noise in a handheld GNSS fix, so an ellipsoidal model would be
// precision we cannot actually measure.
constexpr double kEarthRadiusM = 6371008.8;

// Coordinates are stored as degrees * 1e7 in int32. That is ~1.1 cm of
// resolution, well under the receiver's 1.5 m CEP50, and it keeps every logged
// sample a fixed 8 bytes with no float drift between device and export.
struct Coord {
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;

    static Coord fromDegrees(double lat, double lon);
    double latDeg() const;
    double lonDeg() const;
};

// A fix at exactly (0,0) is overwhelmingly likely to be an unset struct rather
// than a boat off Ghana, so it is treated as invalid along with out-of-range.
bool coordValid(const Coord& c);

double distanceMeters(const Coord& a, const Coord& b);

// Initial great-circle bearing from a to b, degrees clockwise from true north.
double bearingDegrees(const Coord& a, const Coord& b);

// Free-space path loss in dB. The optimistic bound: measured loss below this is
// a sign of something wrong (reflection gain aside), and measured loss far above
// it is the obstruction the survey is looking for.
double fsplDb(double distanceM, double freqMhz);

// Total path loss implied by a transmission of known power. Positive dB.
double pathLossDb(int txDbm, double rxDbm);

// Wraps any angle into [0, 360).
double normalizeDegrees(double deg);

}  // namespace lorascout
