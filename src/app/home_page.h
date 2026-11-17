#pragma once

#include "system/ip_addresses.h"
#include "system/system_stats.h"
#include "wifi/wifi_backend.h"

#include <QTimer>
#include <QWidget>

#include <functional>

class QGridLayout;
class QLabel;
class QProgressBar;

namespace pi_control::app {

struct AppContext;
class SignalBars;

QString formatWifiStatus(const wifi::WifiStatus& status);
QString formatPercent(std::optional<double> percent);
QString formatStorage(quint64 usedBytes, quint64 totalBytes);
QString formatAddresses(const std::vector<system::InterfaceAddress>& addresses);

// Clock, addresses, Wi-Fi and system load at a glance.
class HomePage final : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(const AppContext& context, QWidget* parent = nullptr);

    // Samples addresses and system stats now.
    void refresh();
    // Every visible value, one per line, for --status-report.
    QStringList reportLines() const;

    // Replaces the address source (tests).
    void setAddressSource(std::function<std::vector<system::InterfaceAddress>()> source);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Meter {
        QLabel* name;
        QProgressBar* bar;
        QLabel* value;
    };
    Meter addMeter(QGridLayout* grid, int row, const QString& name);
    void updateClock();
    void scheduleClock();
    void updateWifi();
    static void setMeter(const Meter& meter, std::optional<double> percent);

    void placeColumns(bool twoColumns);

    system::SystemStatsSampler& sampler_;
    wifi::WifiBackend& wifi_;
    std::function<std::vector<system::InterfaceAddress>()> addresses_ = system::currentIpv4Addresses;
    QTimer clockTimer_;
    QTimer statsTimer_;
    QLabel* clock_;
    QLabel* date_;
    QLabel* addressesLabel_;
    QLabel* wifiLabel_;
    SignalBars* wifiBars_;
    QLabel* wifiStrength_;
    Meter cpu_;
    Meter memory_;
    Meter storage_;
    QLabel* storageDetail_;
    QGridLayout* columns_;
    QGridLayout* meters_;
    std::optional<bool> twoColumns_;
};

} // namespace pi_control::app
