#include "wifi/fake_wifi_backend.h"

#include <algorithm>

namespace pi_control::wifi {

namespace {

using State = WifiStatus::State;

WifiNetwork network(const char* ssid, int strength, WifiSecurity security, bool saved = false,
                    QString unsupportedReason = {}) {
    WifiNetwork result;
    result.ssid = QString::fromUtf8(ssid);
    result.ssidBytes = QByteArray(ssid);
    result.strength = strength;
    result.security = security;
    result.unsupportedReason = std::move(unsupportedReason);
    result.saved = saved;
    return result;
}

} // namespace

FakeWifiBackend::FakeWifiBackend(QObject* parent) : WifiBackend(parent) {
    scanTimer_.setSingleShot(true);
    connect(&scanTimer_, &QTimer::timeout, this, &FakeWifiBackend::finishScan);
    connectTimer_.setSingleShot(true);
    connect(&connectTimer_, &QTimer::timeout, this, &FakeWifiBackend::finishConnect);
}

FakeWifiBackend* FakeWifiBackend::withSampleNetworks(QObject* parent) {
    auto* backend = new FakeWifiBackend(parent);
    backend->setNetworks({
        network("HomeNet", 82, WifiSecurity::Personal, true),
        network("Neighbour5G", 64, WifiSecurity::Personal),
        network("CoffeeShop", 41, WifiSecurity::Open),
        network("CorpNet", 38, WifiSecurity::Unsupported, false, "Enterprise"),
        network("Upstairs", 18, WifiSecurity::Personal),
    });
    backend->setPasswordFor("HomeNet", "password123");
    backend->setPasswordFor("Neighbour5G", "password123");
    backend->setPasswordFor("Upstairs", "password123");
    backend->setStatus({.state = State::Connected, .ssid = "HomeNet", .strength = 82});
    return backend;
}

void FakeWifiBackend::setNetworks(std::vector<WifiNetwork> networks) {
    networks_ = std::move(networks);
    emit networksChanged();
}

void FakeWifiBackend::setStatus(WifiStatus status) {
    status_ = std::move(status);
    emit statusChanged();
}

WifiNetwork* FakeWifiBackend::find(const QByteArray& ssidBytes) {
    const auto it = std::find_if(networks_.begin(), networks_.end(),
                                 [&](const WifiNetwork& n) { return n.ssidBytes == ssidBytes; });
    return it == networks_.end() ? nullptr : &*it;
}

std::vector<WifiNetwork> FakeWifiBackend::networks() const {
    if (status_.state == State::Unavailable) return {};
    std::vector<WifiNetwork> result = networks_;
    for (auto& entry : result) entry.active = status_.state == State::Connected && entry.ssid == status_.ssid;
    std::stable_sort(result.begin(), result.end(),
                     [](const WifiNetwork& a, const WifiNetwork& b) { return a.strength > b.strength; });
    return result;
}

void FakeWifiBackend::requestScan() {
    if (scanning_) return;
    scanning_ = true;
    scanTimer_.start(scanDelay_);
}

void FakeWifiBackend::finishScan() {
    if (!scanning_) return;
    scanning_ = false;
    if (vanished_) {
        emit scanFinished(WifiError::Unavailable);
        return;
    }
    if (scanError_ == WifiError::None) {
        lastScanTime_ = QDateTime::currentDateTime();
        emit networksChanged();
    }
    emit scanFinished(scanError_);
}

bool FakeWifiBackend::connectTo(const WifiNetwork& target, const QString& password) {
    if (connecting_) return false;
    ++connectCalls_;
    connecting_ = true;
    pendingSsid_ = target.ssidBytes;
    previousStatus_ = status_;
    if (vanished_) {
        pendingResult_ = WifiError::Unavailable;
    } else if (nextError_) {
        pendingResult_ = *nextError_;
        nextError_.reset();
    } else if (!find(target.ssidBytes)) {
        pendingResult_ = WifiError::NotFound;
    } else if (target.security == WifiSecurity::Personal) {
        const auto expected = passwords_.find(target.ssidBytes);
        const bool savedSecret = target.saved && password.isEmpty();
        pendingResult_ = savedSecret || (expected != passwords_.end() && expected->second == password)
                             ? WifiError::None
                             : WifiError::WrongPassword;
    } else {
        pendingResult_ = WifiError::None;
    }
    QTimer::singleShot(0, this, [this, ssid = target.ssid] {
        if (connecting_ && !vanished_) setStatus({.state = State::Connecting, .ssid = ssid});
    });
    connectTimer_.start(connectDelay_);
    return true;
}

void FakeWifiBackend::finishConnect() {
    if (!connecting_) return;
    const QByteArray ssidBytes = pendingSsid_;
    const WifiError result = pendingResult_;
    if (result == WifiError::None) {
        WifiNetwork* joined = find(ssidBytes);
        if (joined) joined->saved = true;
        setStatus({.state = State::Connected, .ssid = joined ? joined->ssid : QString::fromUtf8(ssidBytes),
                   .strength = joined ? joined->strength : 50});
    } else if (result == WifiError::Unavailable) {
        setStatus({.state = State::Unavailable, .detail = "NetworkManager not running"});
    } else {
        setStatus(previousStatus_);
    }
    const bool recovering = result != WifiError::None && result != WifiError::Unavailable
                            && recoveryDelay_.count() > 0;
    connecting_ = recovering;
    emit networksChanged();
    emit connectFinished(ssidBytes, result);
    if (recovering) {
        QTimer::singleShot(recoveryDelay_, this, [this] {
            connecting_ = false;
            emit statusChanged();
        });
    }
}

void FakeWifiBackend::vanish() {
    vanished_ = true;
    if (connecting_) {
        pendingResult_ = WifiError::Unavailable;
        connectTimer_.start(0);
    }
    if (scanning_) scanTimer_.start(0);
    if (!connecting_) setStatus({.state = State::Unavailable, .detail = "NetworkManager not running"});
}

void FakeWifiBackend::forget(const QByteArray& ssidBytes) {
    ++forgetCalls_;
    QTimer::singleShot(0, this, [this, ssidBytes] {
        if (WifiNetwork* entry = find(ssidBytes)) entry->saved = false;
        emit networksChanged();
        emit forgetFinished(ssidBytes, vanished_ ? WifiError::Unavailable : WifiError::None);
    });
}

} // namespace pi_control::wifi
