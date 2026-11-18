#include "fake_update_starter.h"
#include "release_tree.h"

#include "update/release.h"
#include "update/updater.h"

#include <QDBusObjectPath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace pi_control::update;

namespace {

using namespace std::chrono_literals;

UpdaterOptions options(const ReleaseTree& tree, Version current = {0, 2, 0}) {
    return {.current = current, .releaseBase = tree.base(), .dryRun = true};
}

// A dry-run updater: install() starts nothing.
std::unique_ptr<Updater> dryRun(const ReleaseTree& tree, Version current = {0, 2, 0}) {
    return std::make_unique<Updater>(options(tree, current), nullptr);
}

// A real-mode updater whose unit starts are recorded by the returned fake.
std::pair<std::unique_ptr<Updater>, FakeUpdateStarter*> withFakeStarter(const ReleaseTree& tree,
                                                                        std::chrono::milliseconds timeout = 21min) {
    auto starter = std::make_unique<FakeUpdateStarter>();
    FakeUpdateStarter* fake = starter.get();
    auto opts = options(tree);
    opts.dryRun = false;
    opts.installTimeout = timeout;
    return {std::make_unique<Updater>(opts, std::move(starter)), fake};
}

void checkAndWait(Updater& updater) {
    updater.check();
    QCOMPARE(updater.state(), Updater::State::Checking);
    QTRY_VERIFY(updater.state() != Updater::State::Checking);
}

} // namespace

class UpdaterTests : public QObject {
    Q_OBJECT

private slots:
    void versionsHaveOneSpelling() {
        QCOMPARE(parseVersion(u"0.10.2"), (Version{0, 10, 2}));
        QVERIFY(*parseVersion(u"0.10.0") > *parseVersion(u"0.9.9"));
        QCOMPARE(parseVersion(u"123456789.0.1"), (Version{123456789, 0, 1}));
        for (const char* bad : {"v0.1.2", "1.2", "1.2.3.4", "1.2.x", "1..3", "1.2.3-rc1", "+1.2.3", "01.2.3",
                                "1.2.03", "1234567890.0.0", " 1.2.3", ""}) {
            QVERIFY2(!parseVersion(QString::fromLatin1(bad)), bad);
        }
    }

    void manifestValidationFailsClosed() {
        const QJsonObject good{{"schema", 1},
                               {"version", "1.2.3"},
                               {"sha256", QJsonObject{{"linux-aarch64", QString(64, 'a')}}}};
        const auto parse = [](const QJsonObject& manifest) {
            return parseManifest(QJsonDocument(manifest).toJson());
        };
        const auto broken = [&](const char* key, const QJsonValue& value) {
            QJsonObject manifest = good;
            manifest[key] = value;
            return parse(manifest).error;
        };
        QCOMPARE(parse(good).version, (Version{1, 2, 3}));
        QVERIFY(broken("schema", 2).contains("schema"));
        QVERIFY(broken("schema", "1").contains("schema"));
        QVERIFY(broken("version", "v1.2.3").contains("version"));
        QVERIFY(broken("sha256", QJsonObject{{"linux-aarch64", "abc"}}).contains("checksum"));
        QVERIFY(broken("sha256", QJsonObject{{"linux-aarch64", QString(64, 'A')}}).contains("checksum"));
        QVERIFY(broken("sha256", QJsonObject{{"linux-x86_64", QString(64, 'a')}}).contains("checksum"));
        QVERIFY(!parseManifest("not json").version);
        QVERIFY(!parseManifest("[]").version);
    }

    void newerReleaseIsOffered() {
        ReleaseTree tree("0.3.0");
        auto updater = dryRun(tree);
        QCOMPARE(updater->state(), Updater::State::Idle);
        checkAndWait(*updater);
        QCOMPARE(updater->state(), Updater::State::Available);
        QCOMPARE(updater->latest(), (Version{0, 3, 0}));
        QVERIFY(updater->canInstall());
    }

    void sameOrOlderReleaseIsNotOffered() {
        for (const QByteArray latest : {"0.2.0", "0.1.9"}) {
            ReleaseTree tree(latest);
            auto [updater, starter] = withFakeStarter(tree);
            checkAndWait(*updater);
            QCOMPARE(updater->state(), Updater::State::UpToDate);
            updater->install(); // Nothing to install.
            QCOMPARE(updater->state(), Updater::State::UpToDate);
            QCOMPARE(starter->starts, 0);
        }
    }

    void unusableManifestFailsTheCheck() {
        ReleaseTree tree("0.3.0");
        tree.write("{\"schema\": 1}");
        auto broken = dryRun(tree);
        checkAndWait(*broken);
        QCOMPARE(broken->state(), Updater::State::CheckFailed);
        QVERIFY(!broken->latest());
        QVERIFY(!broken->canInstall());

        tree.write(QByteArray(70 * 1024, ' ') + "{}");
        auto large = dryRun(tree);
        checkAndWait(*large);
        QCOMPARE(large->state(), Updater::State::CheckFailed);

        Updater missing({.current = {0, 2, 0}, .releaseBase = QUrl::fromLocalFile(tree.dir.filePath("nope")),
                         .dryRun = true},
                        nullptr);
        checkAndWait(missing);
        QCOMPARE(missing.state(), Updater::State::CheckFailed);
    }

    void plainHttpIsRefused() {
        Updater updater({.current = {0, 2, 0}, .releaseBase = QUrl("http://example.invalid/releases"), .dryRun = true},
                        nullptr);
        updater.check();
        QCOMPARE(updater.state(), Updater::State::CheckFailed);
    }

    void failedRecheckKeepsTheKnownRelease() {
        ReleaseTree tree("0.3.0");
        auto [updater, starter] = withFakeStarter(tree);
        checkAndWait(*updater);
        tree.write("broken");
        checkAndWait(*updater);
        QCOMPARE(updater->state(), Updater::State::CheckFailed);
        QVERIFY(updater->canInstall());
        updater->install();
        QCOMPARE(updater->state(), Updater::State::Installing);
        QCOMPARE(starter->starts, 1);
    }

    void dryRunInstallStartsNothingAndEnds() {
        ReleaseTree tree("0.3.0");
        auto opts = options(tree);
        opts.dryRunDuration = 50ms;
        Updater updater(opts, nullptr);
        checkAndWait(updater);
        QSignalSpy requested(&updater, &Updater::dryRunInstallRequested);
        updater.install();
        QCOMPARE(updater.state(), Updater::State::Installing);
        QCOMPARE(requested.count(), 1);
        updater.check(); // Not while installing.
        QCOMPARE(updater.state(), Updater::State::Installing);
        QTRY_COMPARE(updater.state(), Updater::State::Available);
    }

    void jobResultDecidesTheOutcome() {
        ReleaseTree tree("0.3.0");
        auto [updater, starter] = withFakeStarter(tree);
        checkAndWait(*updater);
        updater->install();
        QCOMPARE(starter->starts, 1);
        updater->install(); // Already installing.
        QCOMPARE(starter->starts, 1);
        starter->finish(false, "failed");
        QCOMPARE(updater->state(), Updater::State::InstallFailed);

        updater->install();
        QCOMPARE(starter->starts, 2);
        // A finished job that left this process running installed nothing.
        starter->finish(true);
        QCOMPARE(updater->state(), Updater::State::NothingInstalled);
        QVERIFY(!updater->canInstall());
        starter->finish(false, "late"); // Ignored outside Installing.
        QCOMPARE(updater->state(), Updater::State::NothingInstalled);
    }

    void installWithoutAResultTimesOut() {
        ReleaseTree tree("0.3.0");
        auto [updater, starter] = withFakeStarter(tree, 100ms);
        checkAndWait(*updater);
        updater->install();
        QCOMPARE(updater->state(), Updater::State::Installing);
        QTRY_COMPARE(updater->state(), Updater::State::InstallFailed);
        starter->finish(true); // Too late to count.
        QCOMPARE(updater->state(), Updater::State::InstallFailed);
    }

    void unreachableSystemdFailsWithoutStarting() {
        SystemdUpdateStarter starter(QDBusConnection(QStringLiteral("pi-control-test-disconnected")));
        QSignalSpy finished(&starter, &UpdateStarter::finished);
        starter.start();
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toBool(), false);
        QVERIFY(finished.at(0).at(1).toString().contains("cannot watch systemd jobs"));
        starter.start(); // Can be retried.
        QTRY_COMPARE(finished.count(), 2);
    }

    void systemdStarterFollowsOnlyItsOwnJob() {
        SystemdUpdateStarter starter(QDBusConnection(QStringLiteral("pi-control-test-disconnected")));
        QSignalSpy finished(&starter, &UpdateStarter::finished);
        // Without a running start, job signals are ignored.
        QVERIFY(QMetaObject::invokeMethod(&starter, "onJobRemoved", Q_ARG(uint, 1),
                                          Q_ARG(QDBusObjectPath, QDBusObjectPath("/job/1")),
                                          Q_ARG(QString, kUpdateUnit), Q_ARG(QString, "failed")));
        QCOMPARE(finished.count(), 0);
    }

    void periodicChecksRepeat() {
        ReleaseTree tree("0.3.0");
        auto updater = dryRun(tree);
        QSignalSpy changed(updater.get(), &Updater::stateChanged);
        updater->startPeriodicChecks(0ms, 50ms);
        QTRY_VERIFY(changed.count() >= 4); // Checking, Available, Checking, Available.
        QCOMPARE(changed.at(0).at(0).value<Updater::State>(), Updater::State::Checking);
    }
};

QTEST_GUILESS_MAIN(UpdaterTests)
#include "test_updater.moc"
