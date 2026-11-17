#pragma once

#include "wifi/wifi_backend.h"

#include <QAbstractButton>
#include <QPushButton>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <chrono>
#include <optional>
#include <vector>

class QLabel;
class QProgressBar;
class QVBoxLayout;

namespace pi_control::screensaver {
class ScreensaverController;
}

namespace pi_control::app {

class AppShell;
class ElidedLabel;
class PasswordView;
class SignalBars;

inline constexpr int kMaxCollapsedRows = 15;
inline constexpr std::chrono::milliseconds kForgetConfirmWindow{3000};
inline constexpr std::chrono::milliseconds kStatusLineDuration{5000};
inline constexpr std::chrono::seconds kAutoScanAge{30};

// User-facing text for a failed join.
QString connectErrorText(wifi::WifiError error);

// One scanned network. The whole row is the tap target; labels let presses
// through to it.
class NetworkRow final : public QAbstractButton {
    Q_OBJECT

public:
    NetworkRow(const wifi::WifiNetwork& network, std::chrono::milliseconds forgetWindow, QWidget* parent = nullptr);

    const wifi::WifiNetwork& network() const { return network_; }
    QPushButton* forgetButton() const { return forget_; }
    QLabel* stateLabel() const { return state_; }
    void setConnecting(bool connecting, bool target);
    void setForgetWindow(std::chrono::milliseconds window) { confirm_.setInterval(window); }
    // A finger is on the row, or Forget waits for its second tap.
    bool isBusy() const { return isDown() || confirm_.isActive() || (forget_ && forget_->isDown()); }
    QSize sizeHint() const override;

signals:
    void forgetConfirmed(const QByteArray& ssidBytes);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    wifi::WifiNetwork network_;
    QLabel* state_;
    QProgressBar* busy_;
    QPushButton* forget_ = nullptr;
    QTimer confirm_;
};

// Settings section: Wi-Fi status, nearby networks, scan, join and forget.
class WifiPanel final : public QWidget {
    Q_OBJECT

public:
    WifiPanel(wifi::WifiBackend& backend, AppShell& shell, screensaver::ScreensaverController& screensaver,
              QWidget* parent = nullptr);

    void setForgetConfirmWindow(std::chrono::milliseconds window);

    QLabel* headerLabel() const { return header_; }
    QLabel* statusLine() const { return statusLine_; }
    QPushButton* scanButton() const { return scan_; }
    QLabel* lastScanLabel() const { return lastScan_; }
    QPushButton* showAllButton() const { return showAll_; }
    std::vector<NetworkRow*> rows() const;
    NetworkRow* row(const QString& ssid) const;
    // Created on first use.
    PasswordView* passwordView() const;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // Brings the rows in line with the backend, keeping unchanged rows so a
    // press or an armed Forget survives; waits while a row is in use.
    void rebuildRows();
    void refreshRowStates();
    void updateEmptyHint();
    void updateHeader();
    void updateScanControls();
    void updateLastScan();
    void tapped(const wifi::WifiNetwork& network);
    // Asks the backend to join; false while another attempt is under way.
    bool startJoin(const wifi::WifiNetwork& network, const QString& password, bool fromView);
    // Sends a password submitted while the previous attempt was settling.
    void resubmitDeferredPassword();
    void openPasswordView(const wifi::WifiNetwork& network, const QString& message = {});
    void connectFinished(const QByteArray& ssidBytes, wifi::WifiError error);
    void scanFinished(wifi::WifiError error);
    void forgetFinished(const QByteArray& ssidBytes, wifi::WifiError error);
    void showStatus(const QString& text);

    wifi::WifiBackend& backend_;
    AppShell& shell_;
    screensaver::ScreensaverController& screensaver_;
    QLabel* header_;
    QWidget* rowsBox_;
    QVBoxLayout* rowsLayout_;
    QLabel* empty_;
    QPushButton* showAll_;
    QLabel* statusLine_;
    QPushButton* scan_;
    QLabel* lastScan_;
    QPointer<PasswordView> passwordView_;
    // The join this panel started and whose result it is waiting for.
    struct Join {
        QByteArray ssidBytes;
        bool fromView = false; // The password view waits for the result.
    };
    std::optional<Join> join_;
    bool expanded_ = false;
    bool unavailable_ = false;
    std::chrono::milliseconds forgetWindow_ = kForgetConfirmWindow;
    QTimer statusTimer_;
    QTimer rebuildRetry_;
    QTimer lastScanTimer_;
};

} // namespace pi_control::app
