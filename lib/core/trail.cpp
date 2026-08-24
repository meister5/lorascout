#include "trail.h"

#include <cmath>

namespace lorascout {
namespace {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
}

bool Trail::add(const Coord& coord, SignalBand band, float valueDbm) {
    if (!coordValid(coord)) return false;
    Point& p = points_[head_];
    p.coord = coord;
    p.band = band;
    p.valueDbm = valueDbm;
    head_ = (head_ + 1) % kCapacity;
    if (size_ < kCapacity) ++size_;
    return true;
}

const Trail::Point& Trail::at(size_t index) const {
    if (size_ == 0) return points_[0];
    if (index >= size_) index = size_ - 1;
    const size_t oldest = (size_ == kCapacity) ? head_ : 0;
    return points_[(oldest + index) % kCapacity];
}

const Trail::Point& Trail::newest() const {
    if (size_ == 0) return points_[0];
    return points_[(head_ + kCapacity - 1) % kCapacity];
}

void Trail::clear() {
    head_ = 0;
    size_ = 0;
}

Trail::Bounds Trail::bounds() const {
    Bounds b;
    for (size_t i = 0; i < size_; ++i) {
        const Coord& c = at(i).coord;
        if (!b.valid) {
            b.minLatE7 = b.maxLatE7 = c.lat_e7;
            b.minLonE7 = b.maxLonE7 = c.lon_e7;
            b.valid = true;
            continue;
        }
        if (c.lat_e7 < b.minLatE7) b.minLatE7 = c.lat_e7;
        if (c.lat_e7 > b.maxLatE7) b.maxLatE7 = c.lat_e7;
        if (c.lon_e7 < b.minLonE7) b.minLonE7 = c.lon_e7;
        if (c.lon_e7 > b.maxLonE7) b.maxLonE7 = c.lon_e7;
    }
    return b;
}

bool Trail::project(const Coord& coord, int width, int height, int margin,
                    Pixel* out) const {
    if (out == nullptr || width <= 0 || height <= 0) return false;
    const Bounds b = bounds();
    if (!b.valid) return false;

    const int usableW = width - 2 * margin;
    const int usableH = height - 2 * margin;
    if (usableW <= 0 || usableH <= 0) return false;

    const double centreLat = (static_cast<double>(b.minLatE7) +
                              static_cast<double>(b.maxLatE7)) * 0.5 / 1e7;
    // One degree of longitude covers cos(latitude) as much ground as one degree
    // of latitude. Folding that in here is what keeps the trail's shape honest.
    const double lonScale = std::cos(centreLat * kDegToRad);

    const double latSpan = (static_cast<double>(b.maxLatE7) - b.minLatE7) / 1e7;
    const double lonSpan = ((static_cast<double>(b.maxLonE7) - b.minLonE7) / 1e7) * lonScale;

    // A single point, or a perfectly straight line, has zero span on an axis.
    // Fall back to a fixed window rather than dividing by zero.
    constexpr double kMinSpanDeg = 0.00005;  // ~5.5 m
    const double effLat = latSpan > kMinSpanDeg ? latSpan : kMinSpanDeg;
    const double effLon = lonSpan > kMinSpanDeg ? lonSpan : kMinSpanDeg;

    const double scale = std::fmin(static_cast<double>(usableW) / effLon,
                                   static_cast<double>(usableH) / effLat);

    const double centreLon = (static_cast<double>(b.minLonE7) +
                              static_cast<double>(b.maxLonE7)) * 0.5 / 1e7;

    const double dLat = coord.latDeg() - centreLat;
    const double dLon = (coord.lonDeg() - centreLon) * lonScale;

    // Screen y grows downward; north must be up.
    const double x = width * 0.5 + dLon * scale;
    const double y = height * 0.5 - dLat * scale;

    out->x = static_cast<int>(std::lround(x));
    out->y = static_cast<int>(std::lround(y));
    return true;
}

double Trail::spanMeters() const {
    const Bounds b = bounds();
    if (!b.valid) return 0.0;
    const Coord sw{b.minLatE7, b.minLonE7};
    const Coord ne{b.maxLatE7, b.maxLonE7};
    if (!coordValid(sw) || !coordValid(ne)) return 0.0;
    return distanceMeters(sw, ne);
}

}  // namespace lorascout
