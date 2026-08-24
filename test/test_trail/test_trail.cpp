#include "../support/check.h"
#include "trail.h"

using namespace lorascout;

int main() {
    Trail t;
    CHECK_TRUE(t.empty());
    CHECK_NEAR(t.spanMeters(), 0.0, 1e-9);

    Trail::Pixel px;
    CHECK_FALSE(t.project(Coord::fromDegrees(51.5, -0.1), 240, 135, 4, &px));

    // A sample with no fix is refused rather than dragging the bounds to (0,0).
    CHECK_FALSE(t.add(Coord{0, 0}, SignalBand::Good, -90.0f));
    CHECK_TRUE(t.empty());

    // A square-ish walk at 52 N.
    t.add(Coord::fromDegrees(52.0000, -0.0100), SignalBand::Excellent, -70.0f);
    t.add(Coord::fromDegrees(52.0100, -0.0100), SignalBand::Good, -90.0f);
    t.add(Coord::fromDegrees(52.0100, 0.0100), SignalBand::Fair, -100.0f);
    t.add(Coord::fromDegrees(52.0000, 0.0100), SignalBand::Weak, -110.0f);
    CHECK_EQ(t.size(), 4u);

    const Trail::Bounds b = t.bounds();
    CHECK_TRUE(b.valid);
    CHECK_EQ(b.minLatE7, 520000000);
    CHECK_EQ(b.maxLatE7, 520100000);
    CHECK_EQ(b.minLonE7, -100000);
    CHECK_EQ(b.maxLonE7, 100000);

    // Ordering: index 0 is oldest, newest() is the last added.
    CHECK_NEAR(t.at(0).coord.latDeg(), 52.0000, 1e-7);
    CHECK_TRUE(t.at(0).band == SignalBand::Excellent);
    CHECK_TRUE(t.newest().band == SignalBand::Weak);

    // North must be up: a more northerly point projects to a smaller y.
    Trail::Pixel south, north;
    CHECK_TRUE(t.project(Coord::fromDegrees(52.0000, 0.0), 240, 135, 4, &south));
    CHECK_TRUE(t.project(Coord::fromDegrees(52.0100, 0.0), 240, 135, 4, &north));
    CHECK_TRUE(north.y < south.y);

    // East must be right.
    Trail::Pixel west, east;
    CHECK_TRUE(t.project(Coord::fromDegrees(52.005, -0.0100), 240, 135, 4, &west));
    CHECK_TRUE(t.project(Coord::fromDegrees(52.005, 0.0100), 240, 135, 4, &east));
    CHECK_TRUE(east.x > west.x);

    // The centre of the bounds lands at the centre of the screen.
    Trail::Pixel centre;
    CHECK_TRUE(t.project(Coord::fromDegrees(52.0050, 0.0), 240, 135, 4, &centre));
    CHECK_EQ(centre.x, 120);
    CHECK_NEAR(centre.y, 67.5, 1.0);

    // Everything must land inside the box, margins respected.
    for (size_t i = 0; i < t.size(); ++i) {
        Trail::Pixel p;
        CHECK_TRUE(t.project(t.at(i).coord, 240, 135, 4, &p));
        CHECK_TRUE(p.x >= 4 && p.x <= 236);
        CHECK_TRUE(p.y >= 4 && p.y <= 131);
    }

    // The projection must be metric, not per-degree. This walk is 0.02 degrees
    // of longitude (~1370 m at 52 N) by 0.01 of latitude (~1112 m), so the
    // drawn width must exceed the drawn height by roughly that ratio -- not by
    // the 2:1 a naive degree-space projection would produce.
    Trail::Pixel sw, ne;
    t.project(Coord::fromDegrees(52.0000, -0.0100), 240, 135, 4, &sw);
    t.project(Coord::fromDegrees(52.0100, 0.0100), 240, 135, 4, &ne);
    const double drawnW = static_cast<double>(ne.x - sw.x);
    const double drawnH = static_cast<double>(sw.y - ne.y);
    CHECK_TRUE(drawnW > 0.0 && drawnH > 0.0);
    CHECK_NEAR(drawnW / drawnH, 1370.0 / 1112.0, 0.05);

    // Span across the diagonal, checked against the same geodesy the rest of
    // the firmware uses.
    CHECK_NEAR(t.spanMeters(),
               distanceMeters(Coord::fromDegrees(52.0000, -0.0100),
                              Coord::fromDegrees(52.0100, 0.0100)),
               0.01);

    // A single point must not divide by zero; it simply sits in the middle.
    Trail one;
    one.add(Coord::fromDegrees(10.0, 20.0), SignalBand::Good, -80.0f);
    Trail::Pixel solo;
    CHECK_TRUE(one.project(Coord::fromDegrees(10.0, 20.0), 240, 135, 4, &solo));
    CHECK_EQ(solo.x, 120);
    CHECK_NEAR(one.spanMeters(), 0.0, 0.01);

    // A dead-straight line has zero span on one axis and still projects.
    Trail line;
    for (int i = 0; i < 5; ++i) {
        line.add(Coord::fromDegrees(45.0, 9.0 + i * 0.001), SignalBand::Fair, -100.0f);
    }
    for (size_t i = 0; i < line.size(); ++i) {
        Trail::Pixel p;
        CHECK_TRUE(line.project(line.at(i).coord, 240, 135, 4, &p));
        CHECK_TRUE(p.y >= 0 && p.y <= 135);
    }

    // The ring keeps the most recent kCapacity points and drops the oldest.
    Trail ring;
    for (size_t i = 0; i < Trail::kCapacity + 50; ++i) {
        ring.add(Coord::fromDegrees(45.0 + i * 0.0001, 9.0), SignalBand::Good,
                 static_cast<float>(-i));
    }
    CHECK_EQ(ring.size(), Trail::kCapacity);
    CHECK_NEAR(ring.at(0).coord.latDeg(), 45.0 + 50 * 0.0001, 1e-6);
    CHECK_NEAR(ring.newest().coord.latDeg(),
               45.0 + (Trail::kCapacity + 49) * 0.0001, 1e-6);

    ring.clear();
    CHECK_TRUE(ring.empty());
    CHECK_FALSE(ring.bounds().valid);

    return check::finish("trail");
}
