#include "storage.h"

#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "../config.h"

namespace lorascout {
namespace hal {
namespace {

File g_sweep;
File g_packet;
File g_link;
File g_track;

bool appendTo(File& f, const std::string& path, const std::string& header,
              const std::string& row, uint32_t* bytes, uint32_t* errors) {
    if (!f) {
        const bool isNew = !SD.exists(path.c_str());
        f = SD.open(path.c_str(), FILE_APPEND);
        if (!f) {
            ++(*errors);
            return false;
        }
        if (isNew && !header.empty()) {
            *bytes += f.print(header.c_str());
        }
    }
    const size_t written = f.print(row.c_str());
    if (written != row.size()) {
        ++(*errors);
        return false;
    }
    *bytes += written;
    return true;
}

}  // namespace

bool Storage::begin() {
    // Pin numbers come from M5Unified's board profile rather than from
    // constants here: the SD bus differs between Cardputer revisions and the
    // library already knows which board it is running on.
    const int sck = M5.getPin(m5::pin_name_t::sd_spi_sclk);
    const int miso = M5.getPin(m5::pin_name_t::sd_spi_miso);
    const int mosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
    const int cs = M5.getPin(m5::pin_name_t::sd_spi_ss);

    if (sck < 0 || miso < 0 || mosi < 0 || cs < 0) {
        lastError_ = "no microSD pins for this board";
        return false;
    }

    // Usually a no-op: the radio shares this bus and has already brought the
    // host up on the same three pins. Kept so the card still works if the
    // ordering in initHardware ever changes.
    SPI.begin(sck, miso, mosi, cs);
    // The LoRa cap shares this SPI bus, so the card is clocked conservatively;
    // a corrupted log is worse than a slightly slower one.
    if (!SD.begin(cs, SPI, 20000000)) {
        lastError_ = "microSD not detected";
        mounted_ = false;
        return false;
    }

    if (!SD.exists(kRoot)) SD.mkdir(kRoot);
    mounted_ = true;
    return true;
}

bool Storage::openSession(const std::string& dirName) {
    if (!mounted_) return false;
    closeSession();

    sessionPath_ = std::string(kRoot) + "/" + dirName;
    if (!SD.exists(sessionPath_.c_str()) && !SD.mkdir(sessionPath_.c_str())) {
        lastError_ = "could not create session directory";
        sessionPath_.clear();
        return false;
    }
    sessionOpen_ = true;
    lastFlushMs_ = 0;
    return true;
}

void Storage::closeSession() {
    if (g_sweep) g_sweep.close();
    if (g_packet) g_packet.close();
    if (g_link) g_link.close();
    if (g_track) g_track.close();
    sessionOpen_ = false;
}

bool Storage::appendSweep(const std::string& row, const std::string& header) {
    if (!sessionOpen_) return false;
    return appendTo(g_sweep, sessionPath_ + "/sweep.csv", header, row,
                    &bytesWritten_, &writeErrors_);
}

bool Storage::appendPacket(const std::string& row, const std::string& header) {
    if (!sessionOpen_) return false;
    return appendTo(g_packet, sessionPath_ + "/packets.csv", header, row,
                    &bytesWritten_, &writeErrors_);
}

bool Storage::appendLink(const std::string& row, const std::string& header) {
    if (!sessionOpen_) return false;
    return appendTo(g_link, sessionPath_ + "/link.csv", header, row,
                    &bytesWritten_, &writeErrors_);
}

bool Storage::appendTrack(const std::string& row, const std::string& header) {
    if (!sessionOpen_) return false;
    return appendTo(g_track, sessionPath_ + "/track.csv", header, row,
                    &bytesWritten_, &writeErrors_);
}

void Storage::flushIfDue(uint64_t nowMs) {
    if (!sessionOpen_) return;
    if (nowMs - lastFlushMs_ < kFlushIntervalMs) return;
    flushNow();
    lastFlushMs_ = nowMs;
}

void Storage::flushNow() {
    if (g_sweep) g_sweep.flush();
    if (g_packet) g_packet.flush();
    if (g_link) g_link.flush();
    if (g_track) g_track.flush();
}

bool Storage::writeFile(const std::string& relativePath, const std::string& contents) {
    if (!mounted_ || sessionPath_.empty()) return false;
    const std::string path = sessionPath_ + "/" + relativePath;
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) {
        ++writeErrors_;
        lastError_ = "could not open file for writing";
        return false;
    }
    const size_t written = f.print(contents.c_str());
    f.close();
    if (written != contents.size()) {
        ++writeErrors_;
        return false;
    }
    bytesWritten_ += written;
    return true;
}

bool Storage::readFile(const std::string& relativePath, std::string* out) {
    if (!mounted_ || out == nullptr || sessionPath_.empty()) return false;
    const std::string path = sessionPath_ + "/" + relativePath;
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return false;
    out->clear();
    out->reserve(f.size());
    while (f.available()) out->push_back(static_cast<char>(f.read()));
    f.close();
    return true;
}

bool Storage::exists(const std::string& relativePath) const {
    if (!mounted_ || sessionPath_.empty()) return false;
    return SD.exists((sessionPath_ + "/" + relativePath).c_str());
}

std::string Storage::findUnexportedSession() const {
    if (!mounted_) return "";
    File root = SD.open(kRoot);
    if (!root) return "";

    std::string found;
    while (File entry = root.openNextFile()) {
        if (entry.isDirectory()) {
            const std::string dir = entry.name();
            const std::string full = std::string(kRoot) + "/" + dir;
            const bool hasLog = SD.exists((full + "/packets.csv").c_str()) ||
                                SD.exists((full + "/link.csv").c_str()) ||
                                SD.exists((full + "/sweep.csv").c_str());
            const bool exported = SD.exists((full + "/points.geojson").c_str());
            if (hasLog && !exported) found = dir;
        }
        entry.close();
        if (!found.empty()) break;
    }
    root.close();
    return found;
}

}  // namespace hal
}  // namespace lorascout
