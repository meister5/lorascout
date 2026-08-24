// microSD session storage.
//
// The CSVs are opened once and appended to, with an explicit flush cadence: a
// flush per sample would wear the card and stall the writer, while no flush at
// all means a flat battery costs everything since the last block. Flushing on a
// timer bounds the loss to a few seconds of survey.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lorascout {
namespace hal {

class Storage {
public:
    // Root directory on the card. Everything lorascout writes lives under it.
    static constexpr const char* kRoot = "/lorascout";
    static constexpr uint32_t kFlushIntervalMs = 5000;

    bool begin();
    bool available() const { return mounted_; }
    const char* lastError() const { return lastError_; }

    // Creates /lorascout/<dirName>/ and opens the log files for it.
    bool openSession(const std::string& dirName);
    void closeSession();
    bool sessionOpen() const { return sessionOpen_; }
    const std::string& sessionPath() const { return sessionPath_; }

    // Appends one already-serialised line. Writes the header first if the file
    // is new, so a mode that produces no samples leaves no empty file behind.
    bool appendSweep(const std::string& row, const std::string& header);
    bool appendPacket(const std::string& row, const std::string& header);
    bool appendLink(const std::string& row, const std::string& header);
    bool appendTrack(const std::string& row, const std::string& header);

    void flushIfDue(uint64_t nowMs);
    void flushNow();

    // Whole-file writes, used for the derived exports at session close.
    bool writeFile(const std::string& relativePath, const std::string& contents);
    bool readFile(const std::string& relativePath, std::string* out);
    bool exists(const std::string& relativePath) const;

    // Finds a session directory that has canonical CSVs but no derived exports,
    // which is what a power loss mid-session leaves behind. Empty when there is
    // nothing to recover.
    std::string findUnexportedSession() const;

    uint32_t bytesWritten() const { return bytesWritten_; }
    uint32_t writeErrors() const { return writeErrors_; }

private:
    bool mounted_ = false;
    bool sessionOpen_ = false;
    std::string sessionPath_;
    uint64_t lastFlushMs_ = 0;
    uint32_t bytesWritten_ = 0;
    uint32_t writeErrors_ = 0;
    const char* lastError_ = "";
};

}  // namespace hal
}  // namespace lorascout
