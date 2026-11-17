#include "app/wifi_panel.h"

#include "app/app_shell.h"
#include "app/elided_label.h"
#include "app/password_view.h"
#include "app/signal_bars.h"
#include "screensaver/screensaver_controller.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QStyleOption>
#include <QVBoxLayout>

namespace pi_control::app {

namespace {

using State = wifi::WifiStatus::State;
using wifi::WifiError;
using wifi::WifiSecurity;

constexpr int kRowHeight = 64;
const QString kSavedPasswordFailed = QStringLiteral("The saved password didn't work. Enter it again.");

// A small padlock for secured networks; drawn so no icon font is needed.
class LockIcon final : public QWidget {
public:
    LockIcon() {
        setFixedSize(18, 22);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setStyleSheet("background: transparent;");
        setAccessibleName("Secured");
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor color = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                             QPalette::WindowText);
        painter.setPen(QPen(color, 2.4));
        painter.drawArc(QRectF(4, 1.5, 10, 12), 0, 180 * 16);
        painter.drawLine(QPointF(4, 7.5), QPointF(4, 10));
        painter.drawLine(QPointF(14, 7.5), QPointF(14, 10));
        painter.fillRect(QRectF(1, 10, 16, 11), color);
    }
};

QLabel* passThroughLabel(const QString& text = {}) {
    auto* label = new QLabel(text);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    return label;
}

QString rowStateText(const wifi::WifiNetwork& network) {
    if (network.security == WifiSecurity::Unsupported) return network.unsupportedReason + " – not supported";
    if (network.active) return "Connected";
    if (network.saved) return "Saved";
    return {};
}

QString ago(const QDateTime& when) {
    const qint64 seconds = std::max<qint64>(0, when.secsTo(QDateTime::currentDateTime()));
    if (seconds < 60) return QString("Last scan %1 s ago").arg(seconds);
    return QString("Last scan %1 min ago").arg(seconds / 60);
}

} // namespace

QString connectErrorText(WifiError error) {
    switch (error) {
    case WifiError::None: return {};
    case WifiError::WrongPassword: return "Wrong password. Try again.";
    case WifiError::Timeout: return "Couldn't connect in time. Check the password and signal.";
    case WifiError::NotPermitted: return "Pi Control isn't allowed to change Wi‑Fi. See the service journal.";
    case WifiError::NotFound: return "Network is no longer in range.";
    case WifiError::Unavailable:
    case WifiError::Failed: return "Couldn't connect. Try again.";
    }
    return "Couldn't connect. Try again.";
}

NetworkRow::NetworkRow(const wifi::WifiNetwork& network, QWidget* parent)
    : QAbstractButton(parent), network_(network) {
    setObjectName("networkRow");
    setFocusPolicy(Qt::NoFocus);
    setMinimumHeight(kRowHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(network.ssid);
    auto* check = passThroughLabel(network.active ? "✓" : "");
    check->setFixedWidth(24);
    auto* ssid = new ElidedLabel(network.ssid);
    ssid->setObjectName("networkSsid");
    ssid->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* bars = new SignalBars;
    bars->setAttribute(Qt::WA_TransparentForMouseEvents);
    bars->setStrength(network.strength);
    state_ = passThroughLabel(rowStateText(network));
    state_->setObjectName("networkState");
    state_->setStyleSheet("color: #b8c4cf;");
    busy_ = new QProgressBar;
    busy_->setRange(0, 0);
    busy_->setFixedSize(60, 10);
    busy_->setAttribute(Qt::WA_TransparentForMouseEvents);
    busy_->hide();

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 4);
    layout->setSpacing(12);
    layout->addWidget(check);
    layout->addWidget(ssid, 1);
    if (network.security == WifiSecurity::Personal) layout->addWidget(new LockIcon);
    if (network.security != WifiSecurity::Unsupported) layout->addWidget(bars);
    else bars->hide();
    layout->addWidget(state_);
    layout->addWidget(busy_);
    if (network.saved && !network.active) {
        // Never offered for the active network: on a Pi whose only link is
        // Wi-Fi, forgetting it would leave the kiosk offline.
        forget_ = new QPushButton("Forget");
        forget_->setObjectName("forgetButton");
        forget_->setFocusPolicy(Qt::NoFocus);
        confirm_.setSingleShot(true);
        connect(&confirm_, &QTimer::timeout, forget_, [this] { forget_->setText("Forget"); });
        connect(forget_, &QPushButton::clicked, this, [this] {
            if (confirm_.isActive()) {
                confirm_.stop();
                forget_->setText("Forget");
                emit forgetConfirmed(network_.ssidBytes);
                return;
            }
            forget_->setText("Tap again to forget");
            confirm_.start();
        });
        layout->addWidget(forget_);
    }
    setEnabled(network.security != WifiSecurity::Unsupported);
}

QSize NetworkRow::sizeHint() const {
    QSize size = layout()->sizeHint();
    size.setHeight(std::max(size.height(), kRowHeight));
    return size;
}

void NetworkRow::setConnecting(bool connecting, bool target) {
    setEnabled(!connecting && network_.security != WifiSecurity::Unsupported);
    busy_->setVisible(connecting && target);
    state_->setText(connecting && target ? "Connecting…" : rowStateText(network_));
}

void NetworkRow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    const QColor background = isDown() ? QColor("#2c3b48") : network_.active ? QColor("#1c3347") : QColor("#17222c");
    painter.fillPath(path, background);
    if (!isEnabled()) painter.fillPath(path, QColor(16, 24, 32, 110));
}

WifiPanel::WifiPanel(wifi::WifiBackend& backend, AppShell& shell, screensaver::ScreensaverController& screensaver,
                     QWidget* parent)
    : QWidget(parent), backend_(backend), shell_(shell), screensaver_(screensaver) {
    header_ = new QLabel;
    header_->setObjectName("wifiHeader");
    header_->setWordWrap(true);

    rowsBox_ = new QWidget;
    rowsLayout_ = new QVBoxLayout(rowsBox_);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    rowsLayout_->setSpacing(6);
    empty_ = new QLabel("No networks found. Move closer to the router and tap Scan.");
    empty_->setObjectName("hint");
    empty_->setWordWrap(true);
    empty_->hide();
    showAll_ = new QPushButton;
    showAll_->setObjectName("showAllButton");
    showAll_->hide();
    connect(showAll_, &QPushButton::clicked, this, [this] {
        expanded_ = true;
        rebuildRows();
    });

    statusLine_ = new QLabel;
    statusLine_->setObjectName("wifiStatusLine");
    statusLine_->setWordWrap(true);
    statusLine_->hide();

    scan_ = new QPushButton("Scan");
    scan_->setObjectName("scanButton");
    scan_->setMinimumWidth(180);
    lastScan_ = new QLabel;
    lastScan_->setObjectName("hint");
    auto* scanRow = new QHBoxLayout;
    scanRow->setSpacing(16);
    scanRow->addWidget(scan_);
    scanRow->addWidget(lastScan_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    layout->addWidget(header_);
    layout->addWidget(rowsBox_);
    layout->addWidget(empty_);
    layout->addWidget(showAll_);
    layout->addWidget(statusLine_);
    layout->addLayout(scanRow);

    statusTimer_.setSingleShot(true);
    statusTimer_.setInterval(kStatusLineDuration);
    connect(&statusTimer_, &QTimer::timeout, statusLine_, &QLabel::hide);
    lastScanTimer_.setInterval(5000);
    connect(&lastScanTimer_, &QTimer::timeout, this, &WifiPanel::updateLastScan);

    connect(scan_, &QPushButton::clicked, this, [this] {
        backend_.requestScan();
        updateScanControls();
    });
    connect(&backend_, &wifi::WifiBackend::statusChanged, this, [this] {
        updateHeader();
        updateScanControls();
        const bool unavailable = backend_.status().state == State::Unavailable;
        if (unavailable != unavailable_) {
            unavailable_ = unavailable;
            rebuildRows();
        }
        const bool connecting = backend_.isConnecting();
        for (NetworkRow* row : rows()) row->setConnecting(connecting, row->network().ssidBytes == connectingSsid_);
        if (!connecting) connectingSsid_.clear();
        // A password submitted while the previous attempt was still settling.
        if (!connecting && passwordView_ && passwordView_->isConnecting() && !viewAwaitsResult_
            && shell_.currentTask() == passwordView_) {
            if (backend_.connectTo(passwordView_->network(), passwordView_->field()->text())) {
                viewAwaitsResult_ = true;
                connectingSsid_ = passwordView_->network().ssidBytes;
            }
        }
    });
    connect(&backend_, &wifi::WifiBackend::networksChanged, this, &WifiPanel::rebuildRows);
    connect(&backend_, &wifi::WifiBackend::scanFinished, this, &WifiPanel::scanFinished);
    connect(&backend_, &wifi::WifiBackend::connectFinished, this, &WifiPanel::connectFinished);
    connect(&backend_, &wifi::WifiBackend::forgetFinished, this, &WifiPanel::forgetFinished);
    connect(&screensaver_, &screensaver::ScreensaverController::activated, this, [this] {
        if (passwordView_) passwordView_->conceal();
    });

    unavailable_ = backend_.status().state == State::Unavailable;
    updateHeader();
    updateScanControls();
    updateLastScan();
    rebuildRows();
}

PasswordView* WifiPanel::passwordView() const { return passwordView_; }

std::vector<NetworkRow*> WifiPanel::rows() const {
    std::vector<NetworkRow*> list;
    for (int i = 0; i < rowsLayout_->count(); ++i) {
        if (auto* row = qobject_cast<NetworkRow*>(rowsLayout_->itemAt(i)->widget())) list.push_back(row);
    }
    return list;
}

NetworkRow* WifiPanel::row(const QString& ssid) const {
    for (NetworkRow* row : rows()) {
        if (row->network().ssid == ssid) return row;
    }
    return nullptr;
}

void WifiPanel::rebuildRows() {
    for (NetworkRow* row : rows()) {
        rowsLayout_->removeWidget(row);
        row->hide();
        row->deleteLater();
    }
    const bool unavailable = backend_.status().state == State::Unavailable;
    const std::vector<wifi::WifiNetwork> networks = unavailable ? std::vector<wifi::WifiNetwork>{}
                                                                : backend_.networks();
    const std::size_t shown = expanded_ ? networks.size() : std::min<std::size_t>(networks.size(), kMaxCollapsedRows);
    const bool connecting = backend_.isConnecting();
    for (std::size_t i = 0; i < shown; ++i) {
        auto* row = new NetworkRow(networks[i]);
        row->confirm_.setInterval(forgetWindow_);
        row->setConnecting(connecting, networks[i].ssidBytes == connectingSsid_);
        connect(row, &QAbstractButton::clicked, this, [this, row] { tapped(row->network()); });
        connect(row, &NetworkRow::forgetConfirmed, this, [this](const QByteArray& ssid) { backend_.forget(ssid); });
        rowsLayout_->addWidget(row);
    }
    showAll_->setVisible(shown < networks.size());
    showAll_->setText(QString("Show all (%1)").arg(networks.size()));
    empty_->setVisible(!unavailable && networks.empty() && backend_.lastScanTime().has_value());
}

void WifiPanel::updateHeader() {
    const wifi::WifiStatus status = backend_.status();
    switch (status.state) {
    case State::Connected:
        header_->setText(status.ssid.isEmpty() ? "Connected" : "Connected to " + status.ssid);
        break;
    case State::Connecting: header_->setText("Connecting to " + status.ssid + "…"); break;
    case State::Disconnected: header_->setText("Not connected"); break;
    case State::RadioOff: header_->setText("Wi‑Fi is off"); break;
    case State::Unavailable:
        header_->setText(status.detail.isEmpty() ? "Wi‑Fi unavailable" : "Wi‑Fi unavailable — " + status.detail);
        break;
    }
}

void WifiPanel::updateScanControls() {
    const bool unavailable = backend_.status().state == State::Unavailable;
    const bool scanning = backend_.isScanning();
    scan_->setEnabled(!unavailable && !scanning);
    scan_->setText(scanning ? "Scanning…" : "Scan");
}

void WifiPanel::updateLastScan() {
    const auto last = backend_.lastScanTime();
    lastScan_->setText(last ? ago(*last) : "Not scanned yet");
}

void WifiPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateLastScan();
    lastScanTimer_.start();
    const auto last = backend_.lastScanTime();
    const bool stale = !last || last->secsTo(QDateTime::currentDateTime()) > kAutoScanAge.count();
    if (stale && backend_.status().state != State::Unavailable && !backend_.isScanning()) {
        backend_.requestScan();
        updateScanControls();
    }
}

void WifiPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    lastScanTimer_.stop();
}

void WifiPanel::tapped(const wifi::WifiNetwork& network) {
    if (backend_.isConnecting() || network.active || network.security == WifiSecurity::Unsupported) return;
    if (network.security == WifiSecurity::Personal && !network.saved) {
        openPasswordView(network);
        return;
    }
    if (!backend_.connectTo(network, {})) return;
    connectingSsid_ = network.ssidBytes;
    viewAwaitsResult_ = false;
    for (NetworkRow* row : rows()) row->setConnecting(true, row->network().ssidBytes == connectingSsid_);
}

void WifiPanel::openPasswordView(const wifi::WifiNetwork& network, const QString& message) {
    if (!passwordView_) {
        passwordView_ = new PasswordView;
        connect(passwordView_, &PasswordView::submitted, this, [this](const QString& password) {
            if (!backend_.connectTo(passwordView_->network(), password)) return; // Retried on statusChanged.
            viewAwaitsResult_ = true;
            connectingSsid_ = passwordView_->network().ssidBytes;
        });
        connect(passwordView_, &PasswordView::cancelled, this, [this] {
            // An attempt in flight continues; its result shows on the status line.
            viewAwaitsResult_ = false;
            shell_.closeTask();
        });
    }
    passwordView_->open(network, message);
    shell_.showTask(passwordView_);
}

void WifiPanel::connectFinished(const QByteArray& ssidBytes, WifiError error) {
    const auto networks = backend_.networks();
    const auto found = std::find_if(networks.begin(), networks.end(),
                                    [&](const wifi::WifiNetwork& n) { return n.ssidBytes == ssidBytes; });
    wifi::WifiNetwork network;
    if (found != networks.end()) network = *found;
    if (network.ssidBytes.isEmpty()) {
        network.ssidBytes = ssidBytes;
        network.ssid = QString::fromUtf8(ssidBytes);
        network.security = WifiSecurity::Personal;
    }
    const bool fromView = viewAwaitsResult_ && passwordView_ && passwordView_->network().ssidBytes == ssidBytes;
    viewAwaitsResult_ = false;
    if (fromView) {
        if (error == WifiError::None) {
            passwordView_->clear();
            shell_.closeTask();
            showStatus("Connected to " + network.ssid);
        } else {
            passwordView_->showError(connectErrorText(error));
        }
        return;
    }
    if (error == WifiError::None) {
        showStatus("Connected to " + network.ssid);
    } else if (error == WifiError::WrongPassword && shell_.currentTask() == nullptr) {
        openPasswordView(network, kSavedPasswordFailed);
    } else {
        showStatus(network.ssid + ": " + connectErrorText(error));
    }
}

void WifiPanel::scanFinished(WifiError error) {
    updateScanControls();
    updateLastScan();
    switch (error) {
    case WifiError::NotPermitted: showStatus("Pi Control isn't allowed to scan. See the service journal."); break;
    case WifiError::Failed: showStatus("Scan failed. Try again."); break;
    default: break;
    }
    rebuildRows();
}

void WifiPanel::forgetFinished(const QByteArray& ssidBytes, WifiError error) {
    const QString ssid = QString::fromUtf8(ssidBytes);
    if (error == WifiError::None) showStatus("Forgot " + ssid);
    else if (error == WifiError::NotPermitted) showStatus("Pi Control isn't allowed to change Wi‑Fi. See the service journal.");
    else showStatus("Couldn't forget " + ssid + ". Try again.");
}

void WifiPanel::showStatus(const QString& text) {
    statusLine_->setText(text);
    statusLine_->show();
    statusTimer_.start();
}

} // namespace pi_control::app
