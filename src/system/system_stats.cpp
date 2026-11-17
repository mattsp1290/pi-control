#include "system/system_stats.h"

#include <QFile>
#include <QList>

#include <fmt/base.h>

#include <sys/statvfs.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace pi_control::system {

namespace {

std::optional<QByteArray> readFile(const char* path) {
    QFile file(QString::fromLatin1(path));
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    return file.readAll();
}

void logOnce(bool& logged, const char* what) {
    if (logged) return;
    logged = true;
    fmt::print(stderr, "Status: cannot read {}\n", what);
}

} // namespace

std::optional<CpuTimes> parseProcStat(QByteArrayView procStat) {
    const qsizetype end = procStat.indexOf('\n');
    const QByteArray line = procStat.first(end < 0 ? procStat.size() : end).toByteArray();
    const QList<QByteArray> fields = line.simplified().split(' ');
    // cpu user nice system idle iowait irq softirq steal [guest guest_nice]
    if (fields.size() < 9 || fields[0] != "cpu") return std::nullopt;
    quint64 values[8];
    for (int i = 0; i < 8; ++i) {
        bool ok = false;
        values[i] = fields[i + 1].toULongLong(&ok);
        if (!ok) return std::nullopt;
    }
    CpuTimes times;
    times.idle = values[3] + values[4];
    for (const quint64 value : values) times.total += value;
    return times;
}

std::optional<double> cpuPercent(CpuTimes before, CpuTimes after) {
    if (after.total <= before.total || after.idle < before.idle) return std::nullopt;
    const double total = double(after.total - before.total);
    const double idle = double(after.idle - before.idle);
    return std::clamp(100.0 * (1.0 - idle / total), 0.0, 100.0);
}

std::optional<double> parseMemoryPercent(QByteArrayView meminfo) {
    std::optional<quint64> total;
    std::optional<quint64> available;
    for (const QByteArray& line : meminfo.toByteArray().split('\n')) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2) continue;
        bool ok = false;
        const quint64 value = fields[1].toULongLong(&ok);
        if (!ok) continue;
        if (fields[0] == "MemTotal:") total = value;
        else if (fields[0] == "MemAvailable:") available = value;
    }
    if (!total || !available || *total == 0 || *available > *total) return std::nullopt;
    return 100.0 * double(*total - *available) / double(*total);
}

std::optional<double> storagePercent(quint64 blocks, quint64 bfree, quint64 bavail) {
    if (bfree > blocks) return std::nullopt;
    const quint64 used = blocks - bfree;
    if (used + bavail == 0) return std::nullopt;
    return 100.0 * double(used) / double(used + bavail);
}

SystemStats SystemStatsSampler::sample() {
    SystemStats stats;
    std::optional<CpuTimes> cpu;
    if (const auto text = readFile("/proc/stat")) cpu = parseProcStat(*text);
    if (cpu) {
        if (previousCpu_) stats.cpu = cpuPercent(*previousCpu_, *cpu);
        previousCpu_ = cpu;
    } else {
        logOnce(loggedCpu_, "/proc/stat");
    }
    if (const auto text = readFile("/proc/meminfo")) stats.memory = parseMemoryPercent(*text);
    if (!stats.memory) logOnce(loggedMemory_, "/proc/meminfo");
    struct statvfs fs {};
    if (statvfs("/", &fs) == 0) {
        stats.storage = storagePercent(fs.f_blocks, fs.f_bfree, fs.f_bavail);
        const quint64 used = fs.f_blocks >= fs.f_bfree ? fs.f_blocks - fs.f_bfree : 0;
        stats.storageUsedBytes = used * fs.f_frsize;
        stats.storageTotalBytes = (used + fs.f_bavail) * fs.f_frsize;
    }
    if (!stats.storage) logOnce(loggedStorage_, "storage of /");
    return stats;
}

} // namespace pi_control::system
