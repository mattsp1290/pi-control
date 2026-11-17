#include "wifi/connect_attempt.h"
#include "wifi/fake_wifi_backend.h"
#include "wifi/nm_flags.h"
#include "wifi/nm_messages.h"
#include "wifi/nm_wifi_backend.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include <cstdio>
#include <unistd.h>

using namespace pi_control::wifi;
using Kind = ConnectAction::Kind;
using Call = ConnectAttempt::Call;

namespace {

constexpr auto kProfile = "/org/freedesktop/NetworkManager/Settings/9";
constexpr auto kActive = "/org/freedesktop/NetworkManager/ActiveConnection/7";
constexpr auto kPrevious = "/org/freedesktop/NetworkManager/Settings/1";
constexpr auto kTestPassword = "Zq7-unique-test-pw";

std::vector<Kind> kinds(const ConnectActions& actions) {
    std::vector<Kind> result;
    for (const auto& action : actions) result.push_back(action.kind);
    return result;
}

std::vector<WifiError> finishes(const ConnectActions& actions) {
    std::vector<WifiError> result;
    for (const auto& action : actions) {
        if (action.kind == Kind::Finish) result.push_back(action.error);
    }
    return result;
}

// Runs an attempt from a successful AddAndActivate reply onward.
ConnectAttempt startedAttempt(bool created, QString previous = {}) {
    ConnectAttempt attempt(created, std::move(previous));
    const auto actions = attempt.addReplied(kProfile, kActive);
    Q_ASSERT(kinds(actions) == (std::vector<Kind>{Kind::SubscribeActive, Kind::ReadActiveState, Kind::StartTimer}));
    return attempt;
}

WifiNetwork network(const char* ssid, WifiSecurity security, bool saved = false) {
    WifiNetwork result;
    result.ssid = ssid;
    result.ssidBytes = ssid;
    result.strength = 50;
    result.security = security;
    result.saved = saved;
    return result;
}

// Captures everything written to stderr while alive.
class StderrCapture {
public:
    StderrCapture() {
        file_.open();
        std::fflush(stderr);
        saved_ = dup(STDERR_FILENO);
        dup2(file_.handle(), STDERR_FILENO);
    }
    ~StderrCapture() { restore(); }
    QByteArray text() {
        restore();
        file_.seek(0);
        return file_.readAll();
    }

private:
    void restore() {
        if (saved_ < 0) return;
        std::fflush(stderr);
        dup2(saved_, STDERR_FILENO);
        close(saved_);
        saved_ = -1;
    }
    QTemporaryFile file_;
    int saved_ = -1;
};

// The D-Bus signature QtDBus marshals for the message's arguments.
QString signatureOf(const QDBusMessage& message) {
    QString signature;
    for (const QVariant& argument : message.arguments()) {
        signature += QString::fromLatin1(QDBusMetaType::typeToSignature(argument.metaType()));
    }
    return signature;
}

bool argumentsContain(const QSignalSpy& spy, const QString& text) {
    for (const auto& arguments : spy) {
        for (const auto& argument : arguments) {
            if (argument.toString().contains(text) || argument.toByteArray().contains(text.toUtf8())) return true;
        }
    }
    return false;
}

} // namespace

class WifiBackendTests : public QObject {
    Q_OBJECT

private slots:
    void classifiesSecurity() {
        QCOMPARE(classifySecurity(0, 0, 0).security, WifiSecurity::Open);
        QCOMPARE(classifySecurity(kApFlagPrivacy, 0, kKeyMgmtPsk).security, WifiSecurity::Personal);
        QCOMPARE(classifySecurity(kApFlagPrivacy, kKeyMgmtPsk, 0).security, WifiSecurity::Personal);
        QCOMPARE(classifySecurity(kApFlagPrivacy, 0, kKeyMgmtSae).security, WifiSecurity::Personal);
        QCOMPARE(classifySecurity(kApFlagPrivacy, 0, kKeyMgmtPsk | kKeyMgmtSae).security, WifiSecurity::Personal);
        const auto enterprise = classifySecurity(kApFlagPrivacy, 0, kKeyMgmt8021x);
        QCOMPARE(enterprise.security, WifiSecurity::Unsupported);
        QCOMPARE(enterprise.unsupportedReason, QString("Enterprise"));
        const auto wep = classifySecurity(kApFlagPrivacy, 0, 0);
        QCOMPARE(wep.security, WifiSecurity::Unsupported);
        QCOMPARE(wep.unsupportedReason, QString("WEP"));
    }

    void mergesAccessPoints() {
        const std::vector<AccessPointInfo> aps{
            {"/ap/1", "Home", 40, kApFlagPrivacy, 0, kKeyMgmtPsk},
            {"/ap/2", "Home", 70, kApFlagPrivacy, 0, kKeyMgmtPsk},
            {"/ap/3", "", 99, 0, 0, 0},
            {"/ap/4", "Cafe", 70, 0, 0, 0},
            {"/ap/5", "Attic", 20, 0, 0, 0},
        };
        const auto networks = mergeAccessPoints(aps, "Home", {"Attic"});
        QCOMPARE(networks.size(), std::size_t(3));
        QCOMPARE(networks[0].ssid, QString("Cafe")); // Equal strength: by name.
        QCOMPARE(networks[1].ssid, QString("Home"));
        QCOMPARE(networks[1].strength, 70);
        QVERIFY(networks[1].active);
        QCOMPARE(networks[1].security, WifiSecurity::Personal);
        QCOMPARE(networks[2].ssid, QString("Attic"));
        QVERIFY(networks[2].saved);
        QVERIFY(!networks[0].saved && !networks[0].active);
        QCOMPARE(strongestAccessPoint(aps, "Home"), QString("/ap/2"));
        QCOMPARE(strongestAccessPoint(aps, "Gone"), QString("/"));
    }

    void invalidUtf8SsidIsDisplayable() {
        const QByteArray bytes("Caf\xe9", 4);
        const QString shown = displaySsid(bytes);
        QVERIFY(shown.startsWith("Caf"));
        QVERIFY(shown.contains(QChar(0xFFFD)));
    }

    void classifiesFailures() {
        QCOMPARE(classifyConnectFailure(reason::kDeviceNoSecrets, std::nullopt), WifiError::WrongPassword);
        QCOMPARE(classifyConnectFailure(std::nullopt, reason::kActiveNoSecrets), WifiError::WrongPassword);
        QCOMPARE(classifyConnectFailure(reason::kDeviceSsidNotFound, std::nullopt), WifiError::NotFound);
        QCOMPARE(classifyConnectFailure(8, std::nullopt), WifiError::Failed);
        QCOMPARE(classifyConnectFailure(std::nullopt, std::nullopt), WifiError::Failed);
        QCOMPARE(classifyCallError("org.freedesktop.NetworkManager.Settings.PermissionDenied"), WifiError::NotPermitted);
        QCOMPARE(classifyCallError("org.freedesktop.NetworkManager.Device.NotAllowed"), WifiError::NotPermitted);
        QCOMPARE(classifyCallError("org.freedesktop.NetworkManager.UnknownConnection"), WifiError::NotFound);
        QCOMPARE(classifyCallError("org.freedesktop.DBus.Error.NoReply"), WifiError::Failed);
    }

    void activatedBeforeSubscriptionSaves() {
        auto attempt = startedAttempt(true);
        const auto saving = attempt.activeStateRead(active_state::kActivated);
        QCOMPARE(saving, (ConnectActions{{Kind::SaveProfile, kProfile}}));
        const auto done = attempt.saveReplied();
        QCOMPARE(finishes(done), std::vector<WifiError>{WifiError::None});
        QCOMPARE(kinds(done).back(), Kind::RecoveryDone);
        QVERIFY(attempt.isDone());
        QVERIFY(attempt.deviceStateChanged(device_state::kFailed, 100, 7).empty());
    }

    void saveFailureStillSucceeds() {
        auto attempt = startedAttempt(true);
        attempt.activeStateChanged(active_state::kActivated, 0);
        const auto done = attempt.callFailed(Call::Save, "org.freedesktop.NetworkManager.Settings.Failed");
        QCOMPARE(finishes(done), std::vector<WifiError>{WifiError::None});
    }

    void wrongPasswordDeletesProfile() {
        auto attempt = startedAttempt(true);
        QVERIFY(attempt.deviceStateChanged(50, 40, 0).empty());
        QVERIFY(attempt.deviceStateChanged(60, 50, 0).empty());
        const auto failed = attempt.deviceStateChanged(device_state::kFailed, 60, reason::kDeviceNoSecrets);
        QCOMPARE(kinds(failed), (std::vector<Kind>{Kind::DeleteProfile, Kind::Finish, Kind::RecoveryDone}));
        QCOMPARE(failed[0].path, QString(kProfile));
        QCOMPARE(finishes(failed), std::vector<WifiError>{WifiError::WrongPassword});
    }

    void activeObjectGoneThenDeviceFailure() {
        auto attempt = startedAttempt(true);
        QVERIFY(attempt.callFailed(Call::ReadActiveState, "org.freedesktop.DBus.Error.UnknownObject").empty());
        attempt.deviceStateChanged(50, 40, 0);
        const auto failed = attempt.deviceStateChanged(device_state::kDisconnected, 50, reason::kDeviceNoSecrets);
        QCOMPARE(kinds(failed).front(), Kind::DeleteProfile);
        QCOMPARE(finishes(failed), std::vector<WifiError>{WifiError::WrongPassword});
    }

    void savedProfileFailureKeepsProfile() {
        ConnectAttempt attempt(false, {});
        attempt.activateReplied(kActive);
        attempt.deviceStateChanged(60, 50, 0);
        const auto failed = attempt.deviceStateChanged(device_state::kFailed, 60, reason::kDeviceNoSecrets);
        QCOMPARE(kinds(failed), (std::vector<Kind>{Kind::Finish, Kind::RecoveryDone}));
        QCOMPARE(finishes(failed), std::vector<WifiError>{WifiError::WrongPassword});
    }

    void leavingPreviousNetworkIsNotFailure() {
        auto attempt = startedAttempt(true, kPrevious);
        // The previous connection is torn down before the new one starts.
        QVERIFY(attempt.deviceStateChanged(110, 100, 0).empty());
        QVERIFY(attempt.deviceStateChanged(device_state::kDisconnected, 110, 0).empty());
        QVERIFY(attempt.deviceStateChanged(device_state::kPrepare, 30, 0).empty());
        QCOMPARE(kinds(attempt.deviceStateChanged(device_state::kActivated, 90, 0)),
                 std::vector<Kind>{Kind::SaveProfile});
    }

    void timeoutThenLateActivation() {
        auto attempt = startedAttempt(true);
        const auto timedOut = attempt.timerFired();
        QCOMPARE(kinds(timedOut),
                 (std::vector<Kind>{Kind::Deactivate, Kind::DeleteProfile, Kind::Finish, Kind::RecoveryDone}));
        QCOMPARE(timedOut[0].path, QString(kActive));
        QCOMPARE(finishes(timedOut), std::vector<WifiError>{WifiError::Timeout});
        QVERIFY(attempt.activeStateChanged(active_state::kActivated, 0).empty());
        QVERIFY(attempt.deviceStateChanged(device_state::kActivated, 90, 0).empty());
        QVERIFY(attempt.timerFired().empty());
    }

    void permissionDeniedOnAdd() {
        for (const char* name : {"org.freedesktop.NetworkManager.Settings.PermissionDenied",
                                 "org.freedesktop.NetworkManager.Device.NotAllowed"}) {
            ConnectAttempt attempt(true, {});
            const auto failed = attempt.callFailed(Call::AddAndActivate, name);
            QCOMPARE(finishes(failed), std::vector<WifiError>{WifiError::NotPermitted});
            const auto failedKinds = kinds(failed);
            QVERIFY(std::find(failedKinds.begin(), failedKinds.end(), Kind::DeleteProfile) == failedKinds.end());
        }
    }

    void serviceVanished() {
        auto attempt = startedAttempt(true, kPrevious);
        const auto gone = attempt.serviceVanished();
        QCOMPARE(kinds(gone), (std::vector<Kind>{Kind::Finish, Kind::RecoveryDone}));
        QCOMPARE(finishes(gone), std::vector<WifiError>{WifiError::Unavailable});
        QVERIFY(attempt.isDone());
    }

    void failureReturnsToPreviousNetwork() {
        auto attempt = startedAttempt(true, kPrevious);
        attempt.deviceStateChanged(60, 50, 0);
        const auto failed = attempt.deviceStateChanged(device_state::kFailed, 60, reason::kDeviceNoSecrets);
        QCOMPARE(kinds(failed), (std::vector<Kind>{Kind::DeleteProfile, Kind::Finish, Kind::StartTimer}));
        QCOMPARE(failed.back().milliseconds, kRecoveryDelayMs);
        QCOMPARE(attempt.phase(), ConnectAttempt::Phase::Recovering);
        const auto recovered = attempt.timerFired();
        QCOMPARE(recovered, (ConnectActions{{Kind::ActivatePrevious, kPrevious}, {Kind::RecoveryDone}}));
        QVERIFY(attempt.isDone());
    }

    void deviceReconnectsDuringRecovery() {
        auto attempt = startedAttempt(true, kPrevious);
        attempt.deviceStateChanged(60, 50, 0);
        attempt.deviceStateChanged(device_state::kFailed, 60, reason::kDeviceNoSecrets);
        attempt.deviceStateChanged(device_state::kDisconnected, 120, 0);
        attempt.deviceStateChanged(device_state::kPrepare, 30, 0);
        QCOMPARE(attempt.deviceStateChanged(device_state::kActivated, 90, 0), (ConnectActions{{Kind::RecoveryDone}}));
        QVERIFY(attempt.timerFired().empty());
    }

    void cancelledDeletesUnfinishedProfile() {
        auto attempt = startedAttempt(true, kPrevious);
        QCOMPARE(attempt.cancelled(), (ConnectActions{{Kind::DeleteProfile, kProfile}, {Kind::RecoveryDone}}));
    }

    void connectDuringRecoveryIsRejected() {
        NmWifiBackend backend(QDBusConnection(QStringLiteral("pi-control-test-disconnected")));
        auto attempt = startedAttempt(true, kPrevious);
        attempt.deviceStateChanged(60, 50, 0);
        attempt.deviceStateChanged(device_state::kFailed, 60, reason::kDeviceNoSecrets);
        QCOMPARE(attempt.phase(), ConnectAttempt::Phase::Recovering);
        backend.adoptAttemptForTesting(std::move(attempt), "Test");
        QSignalSpy finished(&backend, &WifiBackend::connectFinished);
        QVERIFY(backend.isConnecting());
        QVERIFY(!backend.connectTo(network("Other", WifiSecurity::Open), {}));
        QTest::qWait(50);
        QCOMPARE(finished.count(), 0);
    }

    void unavailableBackendFinishesConnect() {
        NmWifiBackend backend(QDBusConnection(QStringLiteral("pi-control-test-disconnected")));
        QTRY_COMPARE(backend.status().state, WifiStatus::State::Unavailable);
        QSignalSpy finished(&backend, &WifiBackend::connectFinished);
        QSignalSpy scanned(&backend, &WifiBackend::scanFinished);
        QVERIFY(backend.connectTo(network("Other", WifiSecurity::Personal), kTestPassword));
        QVERIFY(backend.isConnecting());
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished[0][1].value<WifiError>(), WifiError::Unavailable);
        QVERIFY(!backend.isConnecting());
        backend.requestScan();
        QTRY_COMPARE(scanned.count(), 1);
        QCOMPARE(scanned[0][0].value<WifiError>(), WifiError::Unavailable);
    }

    void addAndActivateMessage() {
        nm::registerTypes();
        const auto message = nm::buildAddAndActivateMessage("Home", WifiSecurity::Personal, kTestPassword,
                                                            "/dev/3", "/ap/1");
        QCOMPARE(message.member(), QString("AddAndActivateConnection2"));
        QCOMPARE(signatureOf(message), QString("a{sa{sv}}ooa{sv}"));
        const auto settings = message.arguments()[0].value<nm::Settings>();
        QCOMPARE(settings["802-11-wireless"]["ssid"].metaType(), QMetaType::fromType<QByteArray>());
        QCOMPARE(settings["802-11-wireless"]["ssid"].toByteArray(), QByteArray("Home"));
        QCOMPARE(settings["connection"]["type"].toString(), QString("802-11-wireless"));
        QVERIFY(settings.contains("802-11-wireless-security"));
        QCOMPARE(message.arguments()[3].toMap()["persist"].toString(), QString("memory"));

        const auto open = nm::buildAddAndActivateMessage("Cafe", WifiSecurity::Open, {}, "/dev/3", "/");
        QVERIFY(!open.arguments()[0].value<nm::Settings>().contains("802-11-wireless-security"));
        QCOMPARE(signatureOf(nm::buildRequestScanMessage("/dev/3")), QString("a{sv}"));
        QCOMPARE(signatureOf(nm::buildActivateMessage("/p", "/d", "/")), QString("ooo"));
    }

    void passwordNeverLoggedOrSignalled() {
        StderrCapture capture;
        FakeWifiBackend fake;
        fake.setNetworks({network("Home", WifiSecurity::Personal)});
        fake.setPasswordFor("Home", kTestPassword);
        QSignalSpy fakeFinished(&fake, &WifiBackend::connectFinished);
        QSignalSpy fakeStatus(&fake, &WifiBackend::statusChanged);
        QVERIFY(fake.connectTo(network("Home", WifiSecurity::Personal), kTestPassword));
        QTRY_COMPARE(fakeFinished.count(), 1);

        NmWifiBackend backend(QDBusConnection(QStringLiteral("pi-control-test-disconnected")));
        QSignalSpy finished(&backend, &WifiBackend::connectFinished);
        QVERIFY(backend.connectTo(network("Home", WifiSecurity::Personal), kTestPassword));
        QTRY_COMPARE(finished.count(), 1);

        auto attempt = startedAttempt(true, kPrevious);
        attempt.deviceStateChanged(60, 50, 0);
        attempt.deviceStateChanged(device_state::kFailed, 60, reason::kDeviceNoSecrets);

        const QByteArray log = capture.text();
        QVERIFY2(log.contains("Wi‑Fi: connect Home"), log.constData());
        QVERIFY(!log.contains(kTestPassword));
        QVERIFY(!argumentsContain(fakeFinished, kTestPassword));
        QVERIFY(!argumentsContain(finished, kTestPassword));
    }

    void fakeContract() {
        FakeWifiBackend fake;
        fake.setNetworks({network("Home", WifiSecurity::Personal)});
        fake.setPasswordFor("Home", "password123");
        QSignalSpy finished(&fake, &WifiBackend::connectFinished);
        QSignalSpy status(&fake, &WifiBackend::statusChanged);
        QVERIFY(fake.connectTo(network("Home", WifiSecurity::Personal), "wrong-password"));
        QVERIFY(!fake.connectTo(network("Home", WifiSecurity::Personal), "password123"));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished[0][1].value<WifiError>(), WifiError::WrongPassword);
        QVERIFY(status.count() >= 2);
        QVERIFY(fake.connectTo(network("Home", WifiSecurity::Personal), "password123"));
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(finished[1][1].value<WifiError>(), WifiError::None);
        QCOMPARE(fake.status().state, WifiStatus::State::Connected);

        fake.setConnectDelay(std::chrono::milliseconds(500));
        QVERIFY(fake.connectTo(network("Home", WifiSecurity::Personal), "password123"));
        fake.vanish();
        QTRY_COMPARE(finished.count(), 3);
        QCOMPARE(finished[2][1].value<WifiError>(), WifiError::Unavailable);
        QTest::qWait(600);
        QCOMPARE(finished.count(), 3);
    }
};

QTEST_MAIN(WifiBackendTests)
#include "test_wifi_backend.moc"
