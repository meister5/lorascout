#include "geo.h"

#include <cmath>

namespace lorascout {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
}  // namespace

Coord Coord::fromDegrees(double lat, double lon) {
    Coord c;
    c.lat_e7 = static_cast<int32_t>(std::llround(lat * 1e7));
    c.lon_e7 = static_cast<int32_t>(std::llround(lon * 1e7));
    return c;
}

double Coord::latDeg() const { return static_cast<double>(lat_e7) / 1e7; }
double Coord::lonDeg() const { return static_cast<double>(lon_e7) / 1e7; }

bool coordValid(const Coord& c) {
    if (c.lat_e7 == 0 && c.lon_e7 == 0) return false;
    if (c.lat_e7 > 900000000 || c.lat_e7 < -900000000) return false;
    if (c.lon_e7 > 1800000000 || c.lon_e7 < -1800000000) return false;
    return true;
}

double distanceMeters(const Coord& a, const Coord& b) {
    const double lat1 = a.latDeg() * kDegToRad;
    const double lat2 = b.latDeg() * kDegToRad;
    const double dLat = lat2 - lat1;
    const double dLon = (b.lonDeg() - a.lonDeg()) * kDegToRad;

    const double sinLat = std::sin(dLat * 0.5);
    const double sinLon = std::sin(dLon * 0.5);
    double h = sinLat * sinLat + std::cos(lat1) * std::cos(lat2) * sinLon * sinLon;
    if (h > 1.0) h = 1.0;  // guard against rounding pushing asin out of domain
    return 2.0 * kEarthRadiusM * std::asin(std::sqrt(h));
}

double bearingDegrees(const Coord& a, const Coord& b) {
    const double lat1 = a.latDeg() * kDegToRad;
    const double lat2 = b.latDeg() * kDegToRad;
    const double dLon = (b.lonDeg() - a.lonDeg()) * kDegToRad;

    const double y = std::sin(dLon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) -
                     std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
    return normalizeDegrees(std::atan2(y, x) * kRadToDeg);
}

double fsplDb(double distanceM, double freqMhz) {
    if (distanceM <= 0.0 || freqMhz <= 0.0) return 0.0;
    const double km = distanceM / 1000.0;
    return 20.0 * std::log10(km) + 20.0 * std::log10(freqMhz) + 32.44778;
}

double pathLossDb(int txDbm, double rxDbm) {
    return static_cast<double>(txDbm) - rxDbm;
}

double normalizeDegrees(double deg) {
    double d = std::fmod(deg, 360.0);
    if (d < 0.0) d += 360.0;
    return d;
}

}  // namespace lorascout
