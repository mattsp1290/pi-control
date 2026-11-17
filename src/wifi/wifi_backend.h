#pragma once

#include <QObject>
#include <QString>

#include <optional>

namespace pi_control::wifi {

struct WifiStatus {
    enum class State { Unavailable, RadioOff, Disconnected, Connecting, Connected };
    State state = State::Unavailable;
    QString ssid{};               // Connecting or Connected.
    std::optional<int> strength{}; // 0-100 while Connected.
    QString detail{};             // Why Wi-Fi is Unavailable.
};

class WifiBackend : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    virtual WifiStatus status() const = 0;

signals:
    void statusChanged();
};

} // namespace pi_control::wifi
