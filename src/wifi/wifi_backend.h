#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>

#include <optional>
#include <vector>

namespace pi_control::wifi {

struct WifiStatus {
    enum class State { Unavailable, RadioOff, Disconnected, Connecting, Connected };
    State state = State::Unavailable;
    QString ssid{};                // Connecting or Connected.
    std::optional<int> strength{}; // 0-100 while Connected.
    QString detail{};              // Why Wi-Fi is Unavailable.
};

enum class WifiSecurity {
    Open,
    Personal,    // WPA/WPA2/WPA3 with a passphrase (PSK or SAE).
    Unsupported, // 802.1X Enterprise or WEP.
};

struct WifiNetwork {
    QString ssid;         // For display; invalid UTF-8 shows as U+FFFD.
    QByteArray ssidBytes; // Exact bytes: the identity for connect and forget.
    int strength = 0;     // 0-100, strongest access point for this SSID.
    WifiSecurity security = WifiSecurity::Open;
    QString unsupportedReason{}; // "Enterprise" or "WEP" when Unsupported.
    bool active = false;
    bool saved = false;

    bool operator==(const WifiNetwork&) const = default;
};

enum class WifiError { None, WrongPassword, NotPermitted, NotFound, Timeout, Unavailable, Failed };

const char* toString(WifiError error);

// Wi-Fi scanning and connection management. Passwords are never logged,
// never kept beyond the call that needs them, and never part of a signal.
class WifiBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual WifiStatus status() const = 0;
    // One entry per SSID, strongest first, then by SSID; no empty SSIDs.
    virtual std::vector<WifiNetwork> networks() const = 0;
    // Wall-clock time of the last completed scan.
    virtual std::optional<QDateTime> lastScanTime() const = 0;
    virtual void requestScan() = 0;
    virtual bool isScanning() const = 0;
    // True from an accepted connectTo() until connectFinished has been emitted
    // and any return to the previous network has resolved. statusChanged() is
    // emitted when it turns false.
    virtual bool isConnecting() const = 0;
    // False, with no signal, while isConnecting(). Otherwise exactly one
    // connectFinished follows. An empty password reuses a saved profile.
    virtual bool connectTo(const WifiNetwork& network, const QString& password) = 0;
    virtual void forget(const QByteArray& ssidBytes) = 0;

signals:
    void statusChanged();
    void networksChanged();
    void scanFinished(pi_control::wifi::WifiError error); // None, NotPermitted, Unavailable or Failed.
    void connectFinished(const QByteArray& ssidBytes, pi_control::wifi::WifiError error);
    void forgetFinished(const QByteArray& ssidBytes, pi_control::wifi::WifiError error);
};

} // namespace pi_control::wifi
