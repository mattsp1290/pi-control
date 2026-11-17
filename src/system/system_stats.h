#pragma once

#include <QByteArrayView>
#include <QtGlobal>

#include <optional>

namespace pi_control::system {

struct CpuTimes {
    quint64 idle = 0;  // idle + iowait jiffies.
    quint64 total = 0; // Every non-guest column.
};

// The aggregate "cpu" line of /proc/stat.
std::optional<CpuTimes> parseProcStat(QByteArrayView procStat);
// Busy percentage between two samples; nullopt when no time passed.
std::optional<double> cpuPercent(CpuTimes before, CpuTimes after);
// (MemTotal - MemAvailable) / MemTotal from /proc/meminfo.
std::optional<double> parseMemoryPercent(QByteArrayView meminfo);
// The df formula: used / (used + available to unprivileged users).
std::optional<double> storagePercent(quint64 blocks, quint64 bfree, quint64 bavail);

struct SystemStats {
    std::optional<double> cpu;
    std::optional<double> memory;
    std::optional<double> storage;
    quint64 storageUsedBytes = 0;
    quint64 storageTotalBytes = 0; // used + available, as df reports it.
};

// Reads /proc and statvfs("/"). Virtual so tests can script samples.
class SystemStatsSampler {
public:
    virtual ~SystemStatsSampler() = default;
    // The CPU value needs a previous sample: the first call has no cpu.
    virtual SystemStats sample();

private:
    std::optional<CpuTimes> previousCpu_;
    bool loggedCpu_ = false;
    bool loggedMemory_ = false;
    bool loggedStorage_ = false;
};

} // namespace pi_control::system
