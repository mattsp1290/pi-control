#include "app/home_page.h"

#include "app/app_context.h"
#include "app/elided_label.h"
#include "app/signal_bars.h"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

#include <cmath>

namespace pi_control::app {

namespace {

using State = wifi::WifiStatus::State;

constexpr int kStatsIntervalMs = 2000;
constexpr int kTwoColumnMinWidth = 640;

QString formatGigabytes(quint64 bytes) {
    const double gb = double(bytes) / 1e9;
    return gb < 100 ? QString::number(gb, 'f', 1) + " GB" : QString::number(std::round(gb), 'f', 0) + " GB";
}

} // namespace

QString formatWifiStatus(const wifi::WifiStatus& status) {
    switch (status.state) {
    case State::Connected: return "Wi‑Fi  " + (status.ssid.isEmpty() ? QString("Connected") : status.ssid);
    case State::Connecting: return "Wi‑Fi  Connecting to " + status.ssid + "…";
    case State::Disconnected: return "Wi‑Fi  Not connected";
    case State::RadioOff: return "Wi‑Fi  Off";
    case State::Unavailable: return "Wi‑Fi  Unavailable";
    }
    return {};
}

QString formatPercent(std::optional<double> percent) {
    return percent ? QString::number(std::lround(*percent)) + " %" : QString("—");
}

QString formatStorage(quint64 usedBytes, quint64 totalBytes) {
    if (totalBytes == 0) return {};
    return formatGigabytes(usedBytes) + " of " + formatGigabytes(totalBytes);
}

QString formatAddresses(const std::vector<system::InterfaceAddress>& addresses) {
    if (addresses.empty()) return "No network";
    QStringList lines;
    for (const auto& address : addresses) lines << address.interface + "  " + address.address;
    return lines.join('\n');
}

HomePage::HomePage(const AppContext& context, QWidget* parent)
    : QWidget(parent), sampler_(context.stats), wifi_(context.wifi) {
    clock_ = new QLabel;
    clock_->setObjectName("clockLabel");
    clock_->setStyleSheet("font-size: 72px; font-weight: bold;");
    date_ = new QLabel;
    date_->setObjectName("dateLabel");
    date_->setStyleSheet("font-size: 24px; color: #b8c4cf;");
    addressesLabel_ = new QLabel;
    addressesLabel_->setObjectName("addressesLabel");
    wifiLabel_ = new ElidedLabel;
    wifiLabel_->setObjectName("wifiLabel");
    wifiBars_ = new SignalBars;
    wifiStrength_ = new QLabel;
    wifiStrength_->setObjectName("wifiStrengthLabel");

    auto* wifiRow = new QHBoxLayout;
    wifiRow->setSpacing(10);
    wifiRow->addWidget(wifiLabel_);
    wifiRow->addWidget(wifiBars_);
    wifiRow->addWidget(wifiStrength_);
    wifiRow->addStretch();

    auto* left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(clock_);
    left->addWidget(date_);
    left->addSpacing(24);
    left->addWidget(addressesLabel_);
    left->addLayout(wifiRow);
    left->addStretch();

    auto* meters = meters_ = new QGridLayout;
    meters->setHorizontalSpacing(12);
    meters->setVerticalSpacing(16);
    cpu_ = addMeter(meters, 0, "CPU");
    memory_ = addMeter(meters, 1, "Memory");
    storage_ = addMeter(meters, 2, "Storage");
    storageDetail_ = new QLabel;
    storageDetail_->setObjectName("storageDetail");
    storageDetail_->setStyleSheet("color: #b8c4cf;");
    meters->addWidget(storageDetail_, 3, 1, 1, 2);
    meters->setColumnStretch(1, 1);
    meters->setRowStretch(4, 1);

    columns_ = new QGridLayout(this);
    columns_->setContentsMargins(24, 20, 24, 20);
    columns_->setHorizontalSpacing(32);
    columns_->setVerticalSpacing(16);
    columns_->addLayout(left, 0, 0);
    placeColumns(true);

    clockTimer_.setSingleShot(true);
    clockTimer_.setTimerType(Qt::PreciseTimer);
    connect(&clockTimer_, &QTimer::timeout, this, [this] {
        updateClock();
        scheduleClock();
    });
    statsTimer_.setInterval(kStatsIntervalMs);
    connect(&statsTimer_, &QTimer::timeout, this, &HomePage::refresh);
    connect(&wifi_, &wifi::WifiBackend::statusChanged, this, &HomePage::updateWifi);

    updateClock();
    updateWifi();
    setMeter(cpu_, std::nullopt);
    setMeter(memory_, std::nullopt);
    setMeter(storage_, std::nullopt);
    addressesLabel_->setText(formatAddresses(addresses_()));
}

HomePage::Meter HomePage::addMeter(QGridLayout* grid, int row, const QString& name) {
    Meter meter{new QLabel(name), new QProgressBar, new QLabel};
    meter.name->setObjectName(name.toLower() + "Name");
    meter.bar->setObjectName(name.toLower() + "Bar");
    meter.bar->setRange(0, 100);
    meter.bar->setTextVisible(false);
    meter.value->setObjectName(name.toLower() + "Value");
    meter.value->setMinimumWidth(meter.value->fontMetrics().horizontalAdvance("100 %"));
    meter.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(meter.name, row, 0);
    grid->addWidget(meter.bar, row, 1);
    grid->addWidget(meter.value, row, 2);
    return meter;
}

void HomePage::placeColumns(bool twoColumns) {
    if (twoColumns_ == twoColumns) return;
    twoColumns_ = twoColumns;
    columns_->removeItem(meters_);
    columns_->addLayout(meters_, twoColumns ? 0 : 1, twoColumns ? 1 : 0);
    columns_->setColumnStretch(1, twoColumns ? 1 : 0);
    columns_->setColumnStretch(0, 1);
}

void HomePage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    placeColumns(width() >= kTwoColumnMinWidth);
}

void HomePage::setMeter(const Meter& meter, std::optional<double> percent) {
    meter.bar->setValue(percent ? int(std::lround(*percent)) : 0);
    meter.value->setText(formatPercent(percent));
}

void HomePage::setAddressSource(std::function<std::vector<system::InterfaceAddress>()> source) {
    addresses_ = std::move(source);
    addressesLabel_->setText(formatAddresses(addresses_()));
}

void HomePage::refresh() {
    addressesLabel_->setText(formatAddresses(addresses_()));
    const system::SystemStats stats = sampler_.sample();
    setMeter(cpu_, stats.cpu);
    setMeter(memory_, stats.memory);
    setMeter(storage_, stats.storage);
    storageDetail_->setText(formatStorage(stats.storageUsedBytes, stats.storageTotalBytes));
}

void HomePage::updateClock() {
    const QDateTime now = QDateTime::currentDateTime();
    clock_->setText(now.toString("HH:mm"));
    date_->setText(QLocale(QLocale::English).toString(now.date(), "dddd d MMMM"));
}

void HomePage::scheduleClock() {
    // Wake just after the next second boundary so minutes change on time.
    const qint64 msIntoSecond = QDateTime::currentMSecsSinceEpoch() % 1000;
    clockTimer_.start(int(1000 - msIntoSecond) + 5);
}

void HomePage::updateWifi() {
    const wifi::WifiStatus status = wifi_.status();
    wifiLabel_->setText(formatWifiStatus(status));
    const bool connected = status.state == State::Connected;
    wifiBars_->setVisible(connected);
    wifiBars_->setStrength(connected ? status.strength : std::nullopt);
    wifiStrength_->setVisible(connected && status.strength.has_value());
    wifiStrength_->setText(connected && status.strength ? QString::number(*status.strength) + " %" : QString());
}

void HomePage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateClock();
    scheduleClock();
    refresh();
    statsTimer_.start();
}

void HomePage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    clockTimer_.stop();
    statsTimer_.stop();
}

QStringList HomePage::reportLines() const {
    QStringList lines;
    lines << "Time: " + clock_->text() + "  " + date_->text();
    for (const QString& line : addressesLabel_->text().split('\n')) lines << "IP: " + line;
    QString wifi = wifiLabel_->text();
    if (!wifiStrength_->isHidden()) wifi += "  " + wifiStrength_->text();
    lines << wifi;
    lines << "CPU: " + cpu_.value->text();
    lines << "Memory: " + memory_.value->text();
    lines << "Storage: " + storage_.value->text() + "  " + storageDetail_->text();
    return lines;
}

} // namespace pi_control::app
