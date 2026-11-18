#include "fake_update_starter.h"
#include "release_tree.h"

#include "update/release.h"
#include "update/updater.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace pi_control::update;

namespace {

using namespace std::chrono_literals;
using Result = UpdateStarter::Result;

UpdaterOptions options(const ReleaseTree& tree, std::chrono::milliseconds timeout = 21min) {
    return {.current = {0, 2, 0}, .releaseBase = tree.base(), .installTimeout = timeout};
}

std::unique_ptr<Updater> dryRun(const ReleaseTree& tree) {
    return std::make_unique<Updater>(options(tree), std::make_unique<DryRunUpdateStarter>(50ms));
}

// An updater whose unit starts are recorded by the returned fake.
std::pair<std::unique_ptr<Updater>, FakeUpdateStarter*> withFakeStarter(const ReleaseTree& tree,
                                                                        std::chrono::milliseconds timeout = 21min) {
    auto starter = std::make_unique<FakeUpdateStarter>();
    FakeUpdateStarter* fake = starter.get();
    return {std::make_unique<Updater>(options(tree, timeout), std::move(starter)), fake};
}

void checkAndWait(Updater& updater) {
    updater.check();
    QCOMPARE(updater.state(), Updater::State::Checking);
    QTRY_VERIFY(updater.state() != Updater::State::Checking);
}

QString errorOf(const QJsonObject& manifest) {
    const auto parsed = parseManifest(QJsonDocument(manifest).toJson());
    return std::holds_alternative<QString>(parsed) ? std::get<QString>(parsed) : QString();
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
        const auto broken = [&](const char* key, const QJsonValue& value) {
            QJsonObject manifest = good;
            manifest[key] = value;
            return errorOf(manifest);
        };
        const auto parsed = parseManifest(QJsonDocument(good).toJson());
        QCOMPARE(std::get<Version>(parsed), (Version{1, 2, 3}));
        QVERIFY(broken("schema", 2).contains("schema"));
        QVERIFY(broken("schema", "1").contains("schema"));
        QVERIFY(broken("version", "v1.2.3").contains("version"));
        QVERIFY(broken("sha256", QJsonObject{{"linux-aarch64", "abc"}}).contains("checksum"));
        QVERIFY(broken("sha256", QJsonObject{{"linux-aarch64", QString(64, 'A')}}).contains("checksum"));
        QVERIFY(broken("sha256", QJsonObject{{"linux-x86_64", QString(64, 'a')}}).contains("checksum"));
        QVERIFY(std::holds_alternative<QString>(parseManifest("not json")));
        QVERIFY(std::holds_alternative<QString>(parseManifest("[]")));
    }

    void newerReleaseIsOffered() {
        ReleaseTree tree("0.3.0");
        auto updater = dryRun(tree);
        QCOMPARE(updater->state(), Updater::State::Idle);
        QVERIFY(!updater->canInstall());
        checkAndWait(*updater);
        QCOMPARE(updater->state(), Updater::State::Available);
        QCOMPARE(updater->offer(), (Version{0, 3, 0}));
        QVERIFY(updater->canInstall());
    }

    void sameOrOlderReleaseIsNotOffered() {
        for (const QByteArray latest : {"0.2.0", "0.1.9"}) {
            ReleaseTree tree(latest);
            auto [updater, starter] = withFakeStarter(tree);
            checkAndWait(*updater);
            QCOMPARE(updater->state(), Updater::State::UpToDate);
            QVERIFY(!updater->offer());
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
        QVERIFY(!broken->offer());

        tree.write(QByteArray(70 * 1024, ' ') + "{}");
        auto large = dryRun(tree);
        checkAndWait(*large);
        QCOMPARE(large->state(), Updater::State::CheckFailed);

        Updater missing({.current = {0, 2, 0}, .releaseBase = QUrl::fromLocalFile(tree.dir.filePath("nope"))},
                        std::make_unique<DryRunUpdateStarter>());
        checkAndWait(missing);
        QCOMPARE(missing.state(), Updater::State::CheckFailed);
    }

    void plainHttpIsRefused() {
        Updater updater({.current = {0, 2, 0}, .releaseBase = QUrl("http://example.invalid/releases")},
                        std::make_unique<DryRunUpdateStarter>());
        updater.check();
        QCOMPARE(updater.state(), Updater::State::CheckFailed);
    }

    void failedRecheckKeepsTheOffer() {
        ReleaseTree tree("0.3.0");
        auto [updater, starter] = withFakeStarter(tree);
        checkAndWait(*updater);
        tree.write("broken");
        updater->check();
        QVERIFY(updater->offer());
        QVERIFY(!updater->canInstall()); // Not while checking.
        QTRY_COMPARE(updater->state(), Updater::State::CheckFailed);
        QVERIFY(updater->canInstall());
        updater->install();
        QCOMPARE(updater->state(), Updater::State::Installing);
        QCOMPARE(starter->starts, 1);
    }

    void dryRunInstallEnds() {
        ReleaseTree tree("0.3.0");
        auto updater = dryRun(tree);
        checkAndWait(*updater);
        updater->install();
        QCOMPARE(updater->state(), Updater::State::Installing);
        updater->check(); // Not while installing.
        QCOMPARE(updater->state(), Updater::State::Installing);
        QTRY_COMPARE(updater->state(), Updater::State::Available);
    }

    void jobResultDecidesTheOutcome() {
        ReleaseTree tree("0.3.0");
        auto [updater, starter] = withFakeStarter(tree);
        checkAndWait(*updater);
        updater->install();
        QCOMPARE(starter->starts, 1);
        updater->install(); // Already installing.
        QCOMPARE(starter->starts, 1);
        starter->finish(Result::Failed, "failed");
        QCOMPARE(updater->state(), Updater::State::InstallFailed);
        QVERIFY(updater->canInstall());

        updater->install();
        QCOMPARE(starter->starts, 2);
        // A finished job that left this process running installed nothing.
        starter->finish(Result::Done);
        QCOMPARE(updater->state(), Updater::State::NothingInstalled);
        QVERIFY(!updater->offer());
        starter->finish(Result::Failed, "late"); // Ignored outside Installing.
        QCOMPARE(updater->state(), Updater::State::NothingInstalled);
    }

    void installWithoutAResultTimesOut() {
        ReleaseTree tree("0.3.0");
        auto [updater, starter] = withFakeStarter(tree, 100ms);
        checkAndWait(*updater);
        updater->install();
        QCOMPARE(updater->state(), Updater::State::Installing);
        QTRY_COMPARE(updater->state(), Updater::State::InstallFailed);
        starter->finish(Result::Done); // Too late to count.
        QCOMPARE(updater->state(), Updater::State::InstallFailed);
    }

    void systemctlExitStatusIsTheResult() {
        QTemporaryDir dir;
        const auto script = [&](const char* name, const QByteArray& body) {
            const QString path = dir.filePath(name);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) qFatal("cannot write %s", name);
            file.write("#!/bin/sh\n" + body);
            file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
            return path;
        };
        const QString args = dir.filePath("args");
        struct Case {
            QString program;
            Result result;
            QString detail;
        };
        const Case cases[] = {
            {script("ok", "echo \"$@\" > " + args.toUtf8() + "\n"), Result::Done, {}},
            {script("fail", "echo 'Job failed' >&2\nexit 1\n"), Result::Failed, "Job failed"},
            {dir.filePath("missing"), Result::Failed, {}},
        };
        for (const Case& c : cases) {
            SystemctlUpdateStarter starter(c.program);
            QSignalSpy finished(&starter, &UpdateStarter::finished);
            starter.start();
            QTRY_COMPARE(finished.count(), 1);
            QCOMPARE(finished.at(0).at(0).value<Result>(), c.result);
            if (!c.detail.isEmpty()) QCOMPARE(finished.at(0).at(1).toString(), c.detail);
        }
        QFile recorded(args);
        QVERIFY(recorded.open(QIODevice::ReadOnly));
        QCOMPARE(recorded.readAll(), QByteArray("--no-ask-password start pi-control-update.service\n"));
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
