#pragma once

#include "wifi/wifi_backend.h"

#include <QTimer>

#include <chrono>
#include <map>

namespace pi_control::wifi {

// In-memory, scriptable WifiBackend for tests, smoke runs and --fake-wifi.
// Signals are always delivered from the event loop, never from inside a call.
// connectTo emits statusChanged (Connecting), statusChanged (result),
// networksChanged, then connectFinished.
class FakeWifiBackend final : public WifiBackend {
    Q_OBJECT

public:
    explicit FakeWifiBackend(QObject* parent = nullptr);

    // Five networks: a saved and connected one, a secured one (password
    // "password123"), an open one, an Enterprise one and a weak secured one.
    static FakeWifiBackend* withSampleNetworks(QObject* parent = nullptr);

    void setNetworks(std::vector<WifiNetwork> networks);
    void setStatus(WifiStatus status);
    void setScanDelay(std::chrono::milliseconds delay) { scanDelay_ = delay; }
    void setScanError(WifiError error) { scanError_ = error; }
    void setPasswordFor(const QByteArray& ssidBytes, const QString& password) { passwords_[ssidBytes] = password; }
    void setNextError(WifiError error) { nextError_ = error; }
    void setConnectDelay(std::chrono::milliseconds delay) { connectDelay_ = delay; }
    // NetworkManager disappears: a pending connect or scan ends Unavailable.
    void vanish();

    int connectCalls() const { return connectCalls_; }
    int forgetCalls() const { return forgetCalls_; }

    WifiStatus status() const override { return status_; }
    std::vector<WifiNetwork> networks() const override;
    std::optional<QDateTime> lastScanTime() const override { return lastScanTime_; }
    void requestScan() override;
    bool isScanning() const override { return scanning_; }
    bool isConnecting() const override { return connecting_; }
    bool connectTo(const WifiNetwork& network, const QString& password) override;
    void forget(const QByteArray& ssidBytes) override;

private:
    void finishScan();
    void finishConnect();
    WifiNetwork* find(const QByteArray& ssidBytes);

    std::vector<WifiNetwork> networks_;
    WifiStatus status_{.state = WifiStatus::State::Disconnected};
    std::map<QByteArray, QString> passwords_;
    std::chrono::milliseconds scanDelay_{50};
    std::chrono::milliseconds connectDelay_{50};
    WifiError scanError_ = WifiError::None;
    std::optional<WifiError> nextError_;
    std::optional<QDateTime> lastScanTime_;
    bool scanning_ = false;
    bool connecting_ = false;
    bool vanished_ = false;
    QByteArray pendingSsid_;
    WifiError pendingResult_ = WifiError::None;
    WifiStatus previousStatus_;
    QTimer scanTimer_;
    QTimer connectTimer_;
    int connectCalls_ = 0;
    int forgetCalls_ = 0;
};

} // namespace pi_control::wifi
