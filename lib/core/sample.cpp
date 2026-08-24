#include "sample.h"

namespace lorascout {

GeoStamp GeoStamp::from(const GnssFix& fix, uint64_t uptimeMs) {
    GeoStamp g;
    g.uptimeMs = uptimeMs;
    g.utc = fix.time;
    g.coord = fix.coord;
    g.altitudeM = static_cast<float>(fix.altitudeM);
    g.speedKph = static_cast<float>(fix.speedKph);
    g.courseDeg = static_cast<float>(fix.courseDeg);
    g.hdop = static_cast<float>(fix.hdop);
    g.meanCn0 = static_cast<float>(fix.meanCn0);
    g.satsUsed = fix.satsUsed;
    g.satsInView = fix.satsInView;
    g.fixType = fix.fixType;
    g.fixValid = fix.valid;
    return g;
}

bool GeoStamp::surveyGrade() const {
    return fixValid && fixType != FixType::None && coordValid(coord) && hdop <= 10.0f;
}

void computeLinkGeometry(LinkSample& s) {
    s.distanceM = 0.0;
    s.bearingDeg = 0.0;
    s.freeSpacePathLossDb = 0.0;
    s.excessLossDb = 0.0;

    s.measuredPathLossDb = pathLossDb(s.beaconTxDbm, s.packet.rssiDbm);

    // Geometry needs both ends to have a real fix. Without that, the loss figure
    // still stands on its own but distance would be fiction.
    if (!s.beaconFixValid || !coordValid(s.beaconCoord) ||
        !coordValid(s.packet.geo.coord)) {
        return;
    }

    s.distanceM = distanceMeters(s.packet.geo.coord, s.beaconCoord);
    s.bearingDeg = bearingDegrees(s.packet.geo.coord, s.beaconCoord);
    s.freeSpacePathLossDb = fsplDb(s.distanceM, s.packet.radio.freqMhz);
    if (s.freeSpacePathLossDb > 0.0) {
        s.excessLossDb = s.measuredPathLossDb - s.freeSpacePathLossDb;
    }
}

}  // namespace lorascout
