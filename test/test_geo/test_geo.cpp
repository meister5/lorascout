#include "../support/check.h"
#include "geo.h"

using namespace lorascout;

int main() {
    // One degree of longitude at the equator, against the spherical model.
    const Coord origin = Coord::fromDegrees(0.0, 0.0);
    CHECK_NEAR(distanceMeters(origin, Coord::fromDegrees(0.0, 1.0)), 111195.0, 5.0);
    CHECK_NEAR(distanceMeters(origin, Coord::fromDegrees(1.0, 0.0)), 111195.0, 5.0);
    CHECK_NEAR(distanceMeters(origin, origin), 0.0, 0.001);

    // London -> Paris, a distance published widely enough to be a real check.
    const Coord london = Coord::fromDegrees(51.5007, -0.1246);
    const Coord paris = Coord::fromDegrees(48.8566, 2.3522);
    CHECK_NEAR(distanceMeters(london, paris), 342807.0, 50.0);
    CHECK_NEAR(bearingDegrees(london, paris), 148.1, 0.5);

    // Cardinal bearings.
    CHECK_NEAR(bearingDegrees(origin, Coord::fromDegrees(1.0, 0.0)), 0.0, 0.01);
    CHECK_NEAR(bearingDegrees(origin, Coord::fromDegrees(0.0, 1.0)), 90.0, 0.01);
    CHECK_NEAR(bearingDegrees(origin, Coord::fromDegrees(-1.0, 0.0)), 180.0, 0.01);
    CHECK_NEAR(bearingDegrees(origin, Coord::fromDegrees(0.0, -1.0)), 270.0, 0.01);

    // Round-tripping through the int32 representation must hold ~1 cm.
    const Coord c = Coord::fromDegrees(51.5007123, -0.1246789);
    CHECK_NEAR(c.latDeg(), 51.5007123, 1e-7);
    CHECK_NEAR(c.lonDeg(), -0.1246789, 1e-7);
    CHECK_EQ(c.lat_e7, 515007123);

    // (0,0) is treated as "never set" rather than as a real position.
    CHECK_FALSE(coordValid(origin));
    CHECK_TRUE(coordValid(london));
    CHECK_FALSE(coordValid(Coord{950000000, 0}));
    CHECK_FALSE(coordValid(Coord{0, 1900000000}));

    // Free-space path loss: 868 MHz over 1 km.
    CHECK_NEAR(fsplDb(1000.0, 868.0), 91.22, 0.05);
    // Doubling distance costs 6 dB.
    CHECK_NEAR(fsplDb(2000.0, 868.0) - fsplDb(1000.0, 868.0), 6.02, 0.02);
    CHECK_NEAR(fsplDb(0.0, 868.0), 0.0, 0.001);

    CHECK_NEAR(pathLossDb(14, -100.0), 114.0, 0.001);

    CHECK_NEAR(normalizeDegrees(-90.0), 270.0, 0.001);
    CHECK_NEAR(normalizeDegrees(450.0), 90.0, 0.001);

    return check::finish("geo");
}
