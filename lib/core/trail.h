// The live on-screen trail: a bounded ring of recent samples plus the
// projection that fits them to the display.
//
// This lives in lib/ rather than in the UI because the projection is where a
// map goes subtly wrong, and "subtly wrong" is not something you can spot on a
// 240x135 screen. Longitude degrees shrink with latitude, so a projection that
// scales both axes equally stretches every survey east-west by a factor of
// cos(latitude) -- 38% at 52 degrees north.
#pragma once

#include <cstddef>
#include <cstdint>

#include "geo.h"
#include "signalband.h"

namespace lorascout {

class Trail {
public:
    static constexpr size_t kCapacity = 512;

    struct Point {
        Coord coord;
        SignalBand band = SignalBand::None;
        float valueDbm = 0.0f;
    };

    struct Bounds {
        int32_t minLatE7 = 0;
        int32_t maxLatE7 = 0;
        int32_t minLonE7 = 0;
        int32_t maxLonE7 = 0;
        bool valid = false;
    };

    struct Pixel {
        int x = 0;
        int y = 0;
    };

    // Samples without a usable coordinate are rejected rather than drawn at
    // (0,0), which would drag the bounds across the Atlantic.
    bool add(const Coord& coord, SignalBand band, float valueDbm);

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    // Index 0 is the oldest retained point.
    const Point& at(size_t index) const;
    const Point& newest() const;

    Bounds bounds() const;

    // Projects a coordinate into a width x height pixel box, north up, with a
    // uniform metric scale on both axes. Returns false when there is nothing to
    // project against yet.
    bool project(const Coord& coord, int width, int height, int margin, Pixel* out) const;

    // Ground distance spanned by the current bounds, for the scale bar.
    double spanMeters() const;

    void clear();

private:
    Point points_[kCapacity];
    size_t head_ = 0;   // next write position
    size_t size_ = 0;
};

}  // namespace lorascout
