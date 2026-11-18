#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace pi_control::screensaver {
class ScreensaverController;
}

namespace pi_control::app {

struct PowerOptions {
    bool dryRun = false;      // Simulate shutdown; nothing is sent.
    bool failureTest = false; // Send to a disconnected bus so the call always fails.
};

// The Shut down button and its logind PowerOff call.
class PowerPanel final : public QWidget {
    Q_OBJECT

public:
    PowerPanel(PowerOptions options, screensaver::ScreensaverController& screensaver, QWidget* parent = nullptr);

    QPushButton* shutdownButton() const { return button_; }
    // A non-empty reason disables Shut down and shows why; empty allows it again.
    void setBlocked(const QString& reason);
    QLabel* statusLabel() const { return status_; }

signals:
    void shutdownRequested();
    void shutdownFailed(const QString& message);
    void dryRunCompleted();

private:
    void requestShutdown();
    void refresh();
    QString idleText() const;

    PowerOptions options_;
    screensaver::ScreensaverController& screensaver_;
    QLabel* status_;
    QPushButton* button_;
    bool pending_ = false; // A shutdown was requested and has not failed.
    QString blocked_;
    bool showingBlock_ = false; // The status label shows blocked_.
};

} // namespace pi_control::app
