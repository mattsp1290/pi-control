#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <fmt/base.h>

#include "screensaver/registry.h"
#include "screensaver/screensaver_controller.h"
#include "screensaver/screensaver_view.h"

using namespace pi_control::screensaver;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("pi-control");
    QCommandLineParser parser;
    parser.setApplicationDescription("Raspberry Pi shutdown screen");
    parser.addHelpOption();
    parser.addOption({"dry-run", "Simulate shutdown without powering off."});
    parser.addOption({"smoke-test", "Exercise the real button safely and exit."});
    parser.addOption({"smoke-test-failure", "Safely exercise a disconnected shutdown bus and exit."});
    QStringList screensaverIds;
    for (const auto& descriptor : builtinScreensavers()) screensaverIds << descriptor.id;
    const QCommandLineOption screensaverOption(
        "screensaver", "Always show this screensaver: " + screensaverIds.join(", ") + ".", "id");
    parser.addOption(screensaverOption);
    parser.process(app);
    ScreensaverControllerConfig saverConfig;
    if (parser.isSet(screensaverOption)) {
        const QString id = parser.value(screensaverOption);
        const auto* descriptor = findScreensaver(id);
        if (!descriptor) {
            fmt::print(stderr, "Unknown screensaver: {}\n", id.toStdString());
            return 2;
        }
        saverConfig.candidates = {descriptor};
    }
    const bool failureTest = parser.isSet("smoke-test-failure");
    const bool smoke = parser.isSet("smoke-test") || failureTest;
    const bool dryRun = smoke || parser.isSet("dry-run");

    QWidget screen;
    screen.setWindowTitle("Pi Control");
    screen.setMinimumSize(320, 240);
    screen.setStyleSheet("QWidget { background: #101820; color: #ffffff; }"
                         "QLabel { font-size: 22px; }"
                         "QPushButton { background: #b52d3a; border: 3px solid #ef8e98;"
                         "border-radius: 18px; padding: 24px; font-size: 32px; }"
                         "QPushButton:focus { border-color: white; }"
                         "QPushButton:pressed { background: #86202b; }"
                         "QPushButton:disabled { background: #555; }");
    auto* layout = new QVBoxLayout(&screen);
    auto* status = new QLabel(dryRun ? "Pi Control — dry run" : "Pi Control");
    status->setAlignment(Qt::AlignCenter);
    status->setWordWrap(true);
    auto* button = new QPushButton("Shut down");
    button->setAccessibleName("Shut down Raspberry Pi");
    button->setMinimumHeight(110);
    layout->addStretch();
    layout->addWidget(status);
    layout->addSpacing(24);
    layout->addWidget(button);
    layout->addStretch();
    auto* saverView = new ScreensaverView(&screen);
    ScreensaverController saverController(*saverView, std::move(saverConfig));
    bool simulated = false;
    QObject::connect(button, &QPushButton::clicked, &screen, [&] {
        saverController.setInhibited(true); // No screensaver while a shutdown is pending.
        button->setEnabled(false);
        if (dryRun && !failureTest) {
            simulated = true;
            status->setText("Dry run: shutdown requested");
            fmt::print("Dry run: PowerOff(false) was not sent.\n");
            return;
        }
        status->setText("Shutting down…");
        auto message = QDBusMessage::createMethodCall(
            "org.freedesktop.login1", "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager", "PowerOff");
        message << false; // Never prompt for credentials in a boot kiosk.
        // An unregistered connection is always disconnected: tests cannot reach logind.
        const auto bus = failureTest ? QDBusConnection(QStringLiteral("pi-control-test-disconnected"))
                                     : QDBusConnection::systemBus();
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 10000), &screen);
        const auto finished = [=, &app, &saverController](QDBusPendingCallWatcher* call) {
            const QDBusPendingReply<> reply = *call;
            if (reply.isError()) {
                fmt::print(stderr, "Shutdown failed: {}\n", reply.error().message().toStdString());
                status->setText("Shutdown failed. Check the service journal and try again.");
                button->setEnabled(true);
                saverController.setInhibited(false);
            }
            if (failureTest) {
                app.exit(reply.isError() && button->isEnabled()
                         && status->text().startsWith("Shutdown failed") ? 0 : 1);
            }
            call->deleteLater();
        };
        if (watcher->isFinished()) {
            QTimer::singleShot(0, &screen, [=] { finished(watcher); });
        } else {
            QObject::connect(watcher, &QDBusPendingCallWatcher::finished, &screen, finished);
        }
    });
    screen.showFullScreen();
    button->setFocus();
    if (smoke) {
        QTimer::singleShot(0, &screen, [&] {
            button->click();
            if (failureTest) return; // Completion is checked by the asynchronous callback.
            app.exit(simulated && !button->isEnabled()
                     && status->text() == "Dry run: shutdown requested" ? 0 : 1);
        });
    }
    return app.exec();
}
