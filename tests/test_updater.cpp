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

UpdaterOptions options(const ReleaseTree& tree, Version current = {0, 2, 0}) {
    return {.current = current, .releaseBase = tree.base(), .dryRun = true};
}

void checkAndWait(Updater& updater) {
    updater.check();
    QCOMPARE(updater.state(), Updater::State::Checking);
    QTRY_VERIFY(updater.state() != Updater::State::Checking);
}

void jobRemoved(Updater& updater, const QString& unit, const QString& result) {
    QVERIFY(QMetaObject::invokeMethod(&updater, "onJobRemoved", Q_ARG(uint, 7),
                                      Q_ARG(QDBusObjectPath, QDBusObjectPath("/org/freedesktop/systemd1/job/7")),
                                      Q_ARG(QString, unit), Q_ARG(QString, result)));
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
        Updater updater(options(tree));
        QCOMPARE(updater.state(), Updater::State::Idle);
        checkAndWait(updater);
        QCOMPARE(updater.state(), Updater::State::Available);
        QCOMPARE(updater.latest(), (Version{0, 3, 0}));
    }

    void sameOrOlderReleaseIsNotOffered() {
        for (const QByteArray latest : {"0.2.0", "0.1.9"}) {
            ReleaseTree tree(latest);
            Updater updater(options(tree));
            checkAndWait(updater);
            QCOMPARE(updater.state(), Updater::State::UpToDate);
            updater.install(); // Nothing to install.
            QCOMPARE(updater.state(), Updater::State::UpToDate);
        }
    }

    void unusableManifestFailsTheCheck() {
        ReleaseTree tree("0.3.0");
        tree.write("{\"schema\": 1}");
        Updater broken(options(tree));
        checkAndWait(broken);
        QCOMPARE(broken.state(), Updater::State::CheckFailed);
        QVERIFY(!broken.latest());

        tree.write(QByteArray(70 * 1024, ' ') + "{}");
        Updater large(options(tree));
        checkAndWait(large);
        QCOMPARE(large.state(), Updater::State::CheckFailed);

        Updater missing({.current = {0, 2, 0}, .releaseBase = QUrl::fromLocalFile(tree.dir.filePath("nope"))});
        checkAndWait(missing);
        QCOMPARE(missing.state(), Updater::State::CheckFailed);
    }

    void plainHttpIsRefused() {
        Updater updater({.current = {0, 2, 0}, .releaseBase = QUrl("http://example.invalid/releases")});
        updater.check();
        QCOMPARE(updater.state(), Updater::State::CheckFailed);
    }

    void dryRunInstallStartsNothing() {
        ReleaseTree tree("0.3.0");
        Updater updater(options(tree));
        checkAndWait(updater);
        QSignalSpy requested(&updater, &Updater::dryRunInstallRequested);
        updater.install();
        QCOMPARE(updater.state(), Updater::State::Installing);
        QCOMPARE(requested.count(), 1);
        updater.check(); // Not while installing.
        QCOMPARE(updater.state(), Updater::State::Installing);
    }

    void unreachableSystemdFailsTheInstallAndAllowsRetry() {
        ReleaseTree tree("0.3.0");
        auto failing = options(tree);
        failing.failureTest = true;
        Updater updater(failing);
        QVERIFY(!updater.isDryRun());
        checkAndWait(updater);
        updater.install();
        QCOMPARE(updater.state(), Updater::State::Installing);
        QTRY_COMPARE(updater.state(), Updater::State::InstallFailed);
        updater.install();
        QCOMPARE(updater.state(), Updater::State::Installing);
        QTRY_COMPARE(updater.state(), Updater::State::InstallFailed);
    }

    void updaterJobResultDecidesTheOutcome() {
        ReleaseTree tree("0.3.0");
        auto failing = options(tree);
        failing.failureTest = true;
        Updater updater(failing);
        checkAndWait(updater);
        updater.install();
        jobRemoved(updater, "other.service", "failed"); // Someone else's job.
        QCOMPARE(updater.state(), Updater::State::Installing);
        jobRemoved(updater, kUpdateUnit, "failed");
        QCOMPARE(updater.state(), Updater::State::InstallFailed);

        // A finished job that left this process running found nothing to
        // install, so the updater asks again.
        updater.install();
        jobRemoved(updater, kUpdateUnit, "done");
        QCOMPARE(updater.state(), Updater::State::Checking);
        QTRY_COMPARE(updater.state(), Updater::State::Available);
    }

    void periodicChecksRepeat() {
        ReleaseTree tree("0.3.0");
        Updater updater(options(tree));
        QSignalSpy changed(&updater, &Updater::stateChanged);
        using namespace std::chrono_literals;
        updater.startPeriodicChecks(0ms, 50ms);
        QTRY_VERIFY(changed.count() >= 4); // Checking, Available, Checking, Available.
        QCOMPARE(changed.at(0).at(0).value<Updater::State>(), Updater::State::Checking);
    }
};

QTEST_GUILESS_MAIN(UpdaterTests)
#include "test_updater.moc"
