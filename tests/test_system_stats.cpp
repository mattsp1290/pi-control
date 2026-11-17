#include "system/system_stats.h"

#include <QTest>

using namespace pi_control::system;

class SystemStatsTests : public QObject {
    Q_OBJECT

private slots:
    void cpuPercentFromTwoSnapshots() {
        // user nice system idle iowait irq softirq steal guest guest_nice
        const auto before = parseProcStat("cpu  100 0 100 700 100 0 0 0 50 0\ncpu0 1 2 3 4 5 6 7 8 9 10\n");
        const auto after = parseProcStat("cpu  250 0 150 1000 200 0 0 0 90 0\n");
        QVERIFY(before && after);
        QCOMPARE(before->idle, quint64(800));
        QCOMPARE(before->total, quint64(1000)); // Guest columns are ignored.
        // delta total 600, delta idle 400 -> 33.3 % busy.
        const auto percent = cpuPercent(*before, *after);
        QVERIFY(percent);
        QVERIFY(qAbs(*percent - 100.0 / 3.0) < 0.5);
    }

    void malformedProcStat() {
        QVERIFY(!parseProcStat(""));
        QVERIFY(!parseProcStat("cpu0 1 2 3 4 5 6 7 8\n"));
        QVERIFY(!parseProcStat("cpu  1 2 three 4 5 6 7 8\n"));
        QVERIFY(!parseProcStat("cpu  1 2 3\n"));
    }

    void equalSnapshotsHaveNoPercent() {
        const CpuTimes times{10, 100};
        QVERIFY(!cpuPercent(times, times));
    }

    void memoryPercent() {
        const auto percent = parseMemoryPercent("MemTotal:        3884000 kB\nMemFree:  100 kB\n"
                                                "MemAvailable:    2900000 kB\nBuffers: 5 kB\n");
        QVERIFY(percent);
        QVERIFY(qAbs(*percent - 25.33) < 0.01);
        QVERIFY(!parseMemoryPercent("MemTotal: 100 kB\n"));
        QVERIFY(!parseMemoryPercent("MemTotal: 0 kB\nMemAvailable: 0 kB\n"));
    }

    void storagePercentUsesDfFormula() {
        const auto percent = storagePercent(1000, 400, 350);
        QVERIFY(percent);
        QVERIFY(qAbs(*percent - 63.16) < 0.01);
        QVERIFY(!storagePercent(0, 0, 0));
    }

    void samplerReadsThisSystem() {
        SystemStatsSampler sampler;
        const SystemStats first = sampler.sample();
        QVERIFY(!first.cpu);
        QVERIFY(first.memory);
        QVERIFY(first.storage);
        QVERIFY(first.storageTotalBytes > first.storageUsedBytes);
    }
};

QTEST_MAIN(SystemStatsTests)
#include "test_system_stats.moc"
