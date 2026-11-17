#pragma once

#include "wifi/connect_attempt.h"
#include "wifi/nm_access_points.h"
#include "wifi/wifi_backend.h"

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QTimer>

#include <functional>
#include <map>
#include <optional>

class QDBusMessage;

namespace pi_control::wifi {

// Silence after an accepted scan request that means NetworkManager
// rate-limited it and the cached results are current. A real scan on the
// Pi's adapter takes about 4 s.
inline constexpr int kScanQuietMs = 10000;

// WifiBackend over NetworkManager's D-Bus API. Every call is asynchronous;
// connection bookkeeping lives in ConnectAttempt.
class NmWifiBackend final : public WifiBackend {
    Q_OBJECT

public:
    explicit NmWifiBackend(QDBusConnection bus, QObject* parent = nullptr);
    ~NmWifiBackend() override;

    WifiStatus status() const override;
    std::vector<WifiNetwork> networks() const override;
    std::optional<QDateTime> lastScanTime() const override { return lastScanTime_; }
    void requestScan() override;
    bool isScanning() const override { return scanning_; }
    bool isConnecting() const override { return attempt_.has_value(); }
    bool connectTo(const WifiNetwork& network, const QString& password) override;
    void forget(const QByteArray& ssidBytes) override;

    // Test seam: continue an attempt that is already under way (for example
    // in its recovery phase) without NetworkManager.
    void adoptAttemptForTesting(ConnectAttempt attempt, const QByteArray& ssidBytes);

private slots:
    void onManagerPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList&);
    void onDevicePropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList&);
    void onActiveAccessPointPropertiesChanged(const QString& interface, const QVariantMap& changed,
                                              const QStringList&);
    void onDeviceStateChanged(uint newState, uint oldState, uint reason);
    void onActiveStateChanged(uint state, uint reason);
    void onAccessPointAdded(const QDBusObjectPath& path);
    void onAccessPointRemoved(const QDBusObjectPath& path);
    void onConnectionAdded(const QDBusObjectPath& path);
    void onConnectionRemoved(const QDBusObjectPath& path);
    void onDeviceAdded(const QDBusObjectPath& path);
    void onDeviceRemoved(const QDBusObjectPath& path);

private:
    using ReplyHandler = std::function<void(const QDBusMessage&)>;
    using ErrorHandler = std::function<void(const QString& errorName)>;

    struct Subscription {
        QString path;
        QString interface;
        QString name;
        const char* slot;
    };

    void call(const QDBusMessage& message, ReplyHandler onReply, ErrorHandler onError = {});
    void subscribe(const QString& path, const char* interface, const char* name, const char* slot);
    void unsubscribe(const QString& path, const char* name);
    void unsubscribeAll();

    void initialize();
    void findWifiDevice(QList<QDBusObjectPath> devices);
    void useDevice(const QString& path);
    void becomeUnavailable(const QString& detail);
    void clearDevice();
    void applyDeviceProperties(const QVariantMap& properties);
    void setActiveAccessPoint(const QString& path);
    void setActiveConnection(const QString& path);
    void loadAccessPoint(const QString& path, std::function<void()> done = {});
    void reloadAccessPoints(std::function<void()> done);
    void loadProfile(const QString& path);
    void finishScan();
    std::vector<QString> profilesFor(const QByteArray& ssidBytes) const;
    std::vector<AccessPointInfo> accessPoints() const;
    QByteArray activeSsid() const;

    void startAttempt(bool createdProfile);
    // Feed the current attempt. The handlers remember the attempt that made
    // the call, so a late reply never reaches a newer attempt.
    using AttemptStep = std::function<ConnectActions(ConnectAttempt&)>;
    void feed(const AttemptStep& step);
    ReplyHandler attemptReply(std::function<ConnectActions(ConnectAttempt&, const QDBusMessage&)> step);
    ErrorHandler attemptError(ConnectAttempt::Call call);
    void addAndActivate(const WifiNetwork& network, const QString& password);
    void apply(const ConnectActions& actions);

    QDBusConnection bus_;
    QDBusServiceWatcher watcher_;
    quint64 generation_ = 0; // Replies from before a NetworkManager restart are dropped.
    std::vector<Subscription> subscriptions_;

    bool available_ = false;
    QString unavailableDetail_ = QStringLiteral("Starting");
    bool wirelessEnabled_ = true;
    QString devicePath_;
    quint32 deviceState_ = 0;
    QString activeAccessPointPath_;
    QString activeConnectionPath_;
    QString activeProfilePath_;
    std::map<QString, AccessPointInfo> accessPoints_;
    std::map<QString, QByteArray> profiles_; // Wi-Fi profile path -> SSID bytes.

    bool scanning_ = false;
    qint64 lastScan_ = -1; // NetworkManager's LastScan (CLOCK_BOOTTIME ms).
    qint64 scanBaseline_ = -1; // lastScan_ when the scan was requested.
    std::optional<QDateTime> lastScanTime_;
    QTimer scanQuietTimer_;

    std::optional<ConnectAttempt> attempt_;
    quint64 attemptSerial_ = 0; // Identifies attempt_ for late replies.
    QByteArray attemptSsid_;
    // Saved profiles a new password replaces; deleted once the new one works.
    std::vector<QString> supersededProfiles_;
    std::vector<QByteArray> pendingForgets_;
    QString subscribedActivePath_;
    QTimer attemptTimer_;
};

} // namespace pi_control::wifi
