#include "nmea.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lorascout {
namespace {

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Splits in place on commas. Returns the field count.
size_t splitFields(char* s, char** fields, size_t maxFields) {
    size_t n = 0;
    if (maxFields == 0) return 0;
    fields[n++] = s;
    for (char* p = s; *p; ++p) {
        if (*p == ',') {
            *p = '\0';
            if (n < maxFields) fields[n++] = p + 1;
        }
    }
    return n;
}

bool empty(const char* s) { return s == nullptr || s[0] == '\0'; }

double toDouble(const char* s, double fallback = 0.0) {
    if (empty(s)) return fallback;
    return std::strtod(s, nullptr);
}

long toLong(const char* s, long fallback = 0) {
    if (empty(s)) return fallback;
    return std::strtol(s, nullptr, 10);
}

}  // namespace

bool GnssTime::toIso8601(char* out, size_t outLen) const {
    if (out == nullptr || outLen == 0) return false;
    if (!valid) {
        out[0] = '\0';
        return false;
    }
    const int written = std::snprintf(out, outLen,
                                      "%04u-%02u-%02uT%02u:%02u:%02u.%02uZ",
                                      static_cast<unsigned>(year),
                                      static_cast<unsigned>(month),
                                      static_cast<unsigned>(day),
                                      static_cast<unsigned>(hour),
                                      static_cast<unsigned>(minute),
                                      static_cast<unsigned>(second),
                                      static_cast<unsigned>(centisecond));
    return written > 0 && static_cast<size_t>(written) < outLen;
}

uint64_t GnssTime::toUnixSeconds() const {
    if (!valid) return 0;
    // Days from the civil date, by Howard Hinnant's days_from_civil. Shifting
    // the year to start in March makes the leap day the last day of the year,
    // which removes every special case.
    int y = static_cast<int>(year);
    const unsigned m = month;
    const unsigned d = day;
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = static_cast<long long>(era) * 146097 +
                           static_cast<long long>(doe) - 719468;
    if (days < 0) return 0;
    return static_cast<uint64_t>(days) * 86400ull + hour * 3600ull + minute * 60ull + second;
}

bool GnssFix::usableForSurvey() const {
    if (!valid) return false;
    if (fixType == FixType::None) return false;
    if (!coordValid(coord)) return false;
    // Above HDOP 10 the horizontal error is comparable to the distances a
    // walking survey resolves, so the sample is not worth anchoring.
    if (hdop > 10.0) return false;
    if (satsUsed < 4 && fixType == FixType::Fix3D) return false;
    return true;
}

bool nmeaChecksumValid(const char* sentence) {
    if (sentence == nullptr) return false;
    const char* p = sentence;
    if (*p == '$' || *p == '!') ++p;

    uint8_t sum = 0;
    while (*p && *p != '*') {
        sum ^= static_cast<uint8_t>(*p);
        ++p;
    }
    if (*p != '*') return false;
    const int hi = hexVal(p[1]);
    const int lo = hexVal(p[2]);
    if (hi < 0 || lo < 0) return false;
    return sum == static_cast<uint8_t>((hi << 4) | lo);
}

bool parseNmeaCoordinate(const char* field, char hemisphere, double* outDegrees) {
    if (empty(field) || outDegrees == nullptr) return false;

    const char* dot = std::strchr(field, '.');
    const size_t len = std::strlen(field);
    // Degrees occupy everything before the last two digits of the whole part.
    const size_t wholeLen = dot ? static_cast<size_t>(dot - field) : len;
    if (wholeLen < 3) return false;

    char degBuf[8] = {};
    const size_t degLen = wholeLen - 2;
    if (degLen >= sizeof(degBuf)) return false;
    std::memcpy(degBuf, field, degLen);

    const double degrees = std::strtod(degBuf, nullptr);
    const double minutes = std::strtod(field + degLen, nullptr);
    if (minutes >= 60.0) return false;

    double value = degrees + minutes / 60.0;
    if (hemisphere == 'S' || hemisphere == 'W') value = -value;
    if (value > 180.0 || value < -180.0) return false;

    *outDegrees = value;
    return true;
}

void NmeaParser::reset() {
    len_ = 0;
    inSentence_ = false;
    fix_ = GnssFix{};
    updated_ = false;
    sentencesValid_ = 0;
    sentencesUnknown_ = 0;
    checksumErrors_ = 0;
    overruns_ = 0;
    talkerCount_ = 0;
    for (size_t i = 0; i < kMaxTalkers; ++i) talkers_[i] = TalkerState{};
}

bool NmeaParser::takeUpdated() {
    const bool u = updated_;
    updated_ = false;
    return u;
}

bool NmeaParser::encode(char c) {
    if (c == '$') {
        // A '$' mid-sentence means the previous one was truncated by a dropped
        // byte. Restarting is the only safe recovery.
        inSentence_ = true;
        len_ = 0;
        return false;
    }
    if (!inSentence_) return false;

    if (c == '\r' || c == '\n') {
        inSentence_ = false;
        if (len_ == 0) return false;
        buf_[len_] = '\0';
        const bool ok = handleSentence();
        len_ = 0;
        return ok;
    }

    if (len_ + 1 >= kMaxSentence) {
        ++overruns_;
        inSentence_ = false;
        len_ = 0;
        return false;
    }
    buf_[len_++] = c;
    return false;
}

bool NmeaParser::handleSentence() {
    if (!nmeaChecksumValid(buf_)) {
        ++checksumErrors_;
        return false;
    }
    // Trim the checksum before splitting so it never lands in a field.
    if (char* star = std::strchr(buf_, '*')) *star = '\0';

    char* fields[24] = {};
    const size_t n = splitFields(buf_, fields, 24);
    if (n == 0 || std::strlen(fields[0]) < 5) {
        ++sentencesUnknown_;
        return false;
    }

    const char* type = fields[0] + 2;   // skip the two-character talker id
    char talker[3] = {fields[0][0], fields[0][1], '\0'};

    bool handled = false;
    if (std::strcmp(type, "GGA") == 0) {
        handled = handleGga(fields, n);
    } else if (std::strcmp(type, "RMC") == 0) {
        handled = handleRmc(fields, n);
    } else if (std::strcmp(type, "GSA") == 0) {
        handled = handleGsa(fields, n);
    } else if (std::strcmp(type, "GSV") == 0) {
        handled = handleGsv(fields, n, talker);
    }

    if (handled) {
        ++sentencesValid_;
    } else {
        ++sentencesUnknown_;
    }
    return handled;
}

bool NmeaParser::handleGga(char* const* f, size_t n) {
    if (n < 10) return false;

    const long quality = toLong(f[6], 0);
    if (quality > 0) {
        double lat = 0.0;
        double lon = 0.0;
        const bool haveLat = parseNmeaCoordinate(f[2], empty(f[3]) ? 'N' : f[3][0], &lat);
        const bool haveLon = parseNmeaCoordinate(f[4], empty(f[5]) ? 'E' : f[5][0], &lon);
        if (haveLat && haveLon) {
            fix_.coord = Coord::fromDegrees(lat, lon);
            updated_ = true;
        }
    }

    fix_.satsUsed = static_cast<uint8_t>(toLong(f[7], 0));
    if (!empty(f[8])) fix_.hdop = toDouble(f[8], 99.99);
    if (n > 9 && !empty(f[9])) fix_.altitudeM = toDouble(f[9]);
    if (n > 11 && !empty(f[11])) fix_.geoidSepM = toDouble(f[11]);

    // GGA quality 0 means no fix, regardless of what a stale RMC still says.
    if (quality == 0) fix_.valid = false;
    return true;
}

bool NmeaParser::handleRmc(char* const* f, size_t n) {
    if (n < 10) return false;

    fix_.valid = (!empty(f[2]) && f[2][0] == 'A');

    if (!empty(f[1]) && std::strlen(f[1]) >= 6) {
        char part[4] = {};
        std::memcpy(part, f[1] + 0, 2);
        fix_.time.hour = static_cast<uint8_t>(std::atoi(part));
        std::memcpy(part, f[1] + 2, 2);
        fix_.time.minute = static_cast<uint8_t>(std::atoi(part));
        std::memcpy(part, f[1] + 4, 2);
        fix_.time.second = static_cast<uint8_t>(std::atoi(part));
        fix_.time.centisecond = 0;
        if (const char* dot = std::strchr(f[1], '.')) {
            const double frac = std::strtod(dot, nullptr);
            fix_.time.centisecond = static_cast<uint16_t>(frac * 100.0 + 0.5);
        }
    }

    if (!empty(f[9]) && std::strlen(f[9]) >= 6) {
        char part[4] = {};
        std::memcpy(part, f[9] + 0, 2);
        fix_.time.day = static_cast<uint8_t>(std::atoi(part));
        std::memcpy(part, f[9] + 2, 2);
        fix_.time.month = static_cast<uint8_t>(std::atoi(part));
        std::memcpy(part, f[9] + 4, 2);
        // NMEA carries two-digit years. The receiver's own epoch starts well
        // after 2000, so there is no ambiguity worth modelling.
        fix_.time.year = static_cast<uint16_t>(2000 + std::atoi(part));
        fix_.time.valid = fix_.time.month >= 1 && fix_.time.month <= 12 &&
                          fix_.time.day >= 1 && fix_.time.day <= 31;
    }

    if (!empty(f[7])) fix_.speedKph = toDouble(f[7]) * 1.852;  // knots
    if (!empty(f[8])) fix_.courseDeg = normalizeDegrees(toDouble(f[8]));

    if (fix_.valid) {
        double lat = 0.0;
        double lon = 0.0;
        if (parseNmeaCoordinate(f[3], empty(f[4]) ? 'N' : f[4][0], &lat) &&
            parseNmeaCoordinate(f[5], empty(f[6]) ? 'E' : f[6][0], &lon)) {
            fix_.coord = Coord::fromDegrees(lat, lon);
            updated_ = true;
        }
    }
    return true;
}

bool NmeaParser::handleGsa(char* const* f, size_t n) {
    if (n < 18) return false;
    const long mode = toLong(f[2], 1);
    if (mode >= 1 && mode <= 3) fix_.fixType = static_cast<FixType>(mode);
    if (!empty(f[15])) fix_.pdop = toDouble(f[15], 99.99);
    if (!empty(f[16])) fix_.hdop = toDouble(f[16], 99.99);
    if (!empty(f[17])) fix_.vdop = toDouble(f[17], 99.99);
    return true;
}

bool NmeaParser::handleGsv(char* const* f, size_t n, const char* talker) {
    if (n < 4) return false;

    const long totalMsgs = toLong(f[1], 1);
    const long msgNum = toLong(f[2], 1);
    const long inView = toLong(f[3], 0);

    TalkerState* t = talkerSlot(talker);
    if (t == nullptr) return true;  // more constellations than slots; ignore

    t->inView = static_cast<uint8_t>(inView > 255 ? 255 : inView);

    if (msgNum <= 1) {
        t->accumSum = 0;
        t->accumCount = 0;
        t->accumMax = 0;
    }
    for (size_t i = 4; i + 3 < n; i += 4) {
        const char* snrField = f[i + 3];
        if (empty(snrField)) continue;      // satellite in view but not tracked
        const long snr = toLong(snrField, 0);
        if (snr <= 0) continue;
        t->accumSum += static_cast<uint32_t>(snr);
        ++t->accumCount;
        if (snr > t->accumMax) t->accumMax = static_cast<uint8_t>(snr);
    }

    // Publish only on the final message of this talker's cycle, so a partial
    // cycle never yields a misleadingly small sample.
    if (msgNum >= totalMsgs) {
        t->cn0Sum = t->accumSum;
        t->cn0Count = t->accumCount;
        t->cn0Max = t->accumMax;
    }

    recomputeAggregates();
    return true;
}

NmeaParser::TalkerState* NmeaParser::talkerSlot(const char* talker) {
    for (size_t i = 0; i < talkerCount_; ++i) {
        if (std::strncmp(talkers_[i].id, talker, 2) == 0) return &talkers_[i];
    }
    if (talkerCount_ >= kMaxTalkers) return nullptr;
    TalkerState* t = &talkers_[talkerCount_++];
    t->id[0] = talker[0];
    t->id[1] = talker[1];
    t->id[2] = '\0';
    return t;
}

void NmeaParser::recomputeAggregates() {
    unsigned inView = 0;
    uint32_t sum = 0;
    uint32_t count = 0;
    uint8_t max = 0;
    for (size_t i = 0; i < talkerCount_; ++i) {
        inView += talkers_[i].inView;
        sum += talkers_[i].cn0Sum;
        count += talkers_[i].cn0Count;
        if (talkers_[i].cn0Max > max) max = talkers_[i].cn0Max;
    }
    fix_.satsInView = static_cast<uint8_t>(inView > 255 ? 255 : inView);
    fix_.maxCn0 = max;
    fix_.meanCn0 = count > 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
}

}  // namespace lorascout
