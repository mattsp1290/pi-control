#include "app/power_panel.h"

#include "screensaver/screensaver_controller.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <fmt/base.h>

namespace pi_control::app {

PowerPanel::PowerPanel(PowerOptions options, screensaver::ScreensaverController& screensaver, QWidget* parent)
    : QWidget(parent), options_(options), screensaver_(screensaver) {
    status_ = new QLabel(idleText());
    status_->setObjectName("powerStatus");
    status_->setWordWrap(true);
    button_ = new QPushButton("Shut down");
    button_->setObjectName("shutdownButton");
    button_->setAccessibleName("Shut down Raspberry Pi");
    button_->setMinimumHeight(110);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(status_);
    layout->addWidget(button_);
    connect(button_, &QPushButton::clicked, this, &PowerPanel::requestShutdown);
}

QString PowerPanel::idleText() const { return options_.dryRun ? "Dry run: shutdown is simulated." : ""; }

void PowerPanel::setBlocked(const QString& reason) {
    if (blocked_ == reason) return;
    blocked_ = reason;
    if (!reason.isEmpty() && !pending_) {
        status_->setText(reason);
        showingBlock_ = true;
    } else if (reason.isEmpty() && showingBlock_) {
        status_->setText(idleText());
        showingBlock_ = false;
    }
    refresh();
}

void PowerPanel::refresh() { button_->setEnabled(!pending_ && blocked_.isEmpty()); }

void PowerPanel::requestShutdown() {
    if (pending_ || !blocked_.isEmpty()) return;
    screensaver_.setInhibited(this, true); // No screensaver while a shutdown is pending.
    pending_ = true;
    showingBlock_ = false;
    refresh();
    emit shutdownRequested();
    if (options_.dryRun && !options_.failureTest) {
        status_->setText("Dry run: shutdown requested");
        fmt::print("Dry run: PowerOff(false) was not sent.\n");
        emit dryRunCompleted();
        return;
    }
    status_->setText("Shutting down…");
    auto message = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1",
                                                  "org.freedesktop.login1.Manager", "PowerOff");
    message << false; // Never prompt for credentials in a boot kiosk.
    // An unregistered connection is always disconnected: tests cannot reach logind.
    const auto bus = options_.failureTest ? QDBusConnection(QStringLiteral("pi-control-test-disconnected"))
                                          : QDBusConnection::systemBus();
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 10000), this);
    const auto finished = [this](QDBusPendingCallWatcher* call) {
        const QDBusPendingReply<> reply = *call;
        if (reply.isError()) {
            const QString detail = reply.error().message();
            fmt::print(stderr, "Shutdown failed: {}\n", detail.toStdString());
            status_->setText("Shutdown failed. Check the service journal and try again.");
            showingBlock_ = false;
            pending_ = false;
            refresh();
            screensaver_.setInhibited(this, false);
            emit shutdownFailed(detail);
        }
        call->deleteLater();
    };
    if (watcher->isFinished()) {
        QTimer::singleShot(0, this, [=] { finished(watcher); });
    } else {
        connect(watcher, &QDBusPendingCallWatcher::finished, this, finished);
    }
}

} // namespace pi_control::app
