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

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("pi-control");
    QCommandLineParser parser;
    parser.setApplicationDescription("Raspberry Pi shutdown screen");
    parser.addHelpOption();
    parser.addOption({"dry-run", "Simulate shutdown without powering off."});
    parser.addOption({"smoke-test", "Exercise the real button safely and exit."});
    parser.process(app);
    const bool smoke = parser.isSet("smoke-test");
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
    bool simulated = false;
    QObject::connect(button, &QPushButton::clicked, &screen, [&] {
        button->setEnabled(false);
        if (dryRun) {
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
        auto* watcher = new QDBusPendingCallWatcher(
            QDBusConnection::systemBus().asyncCall(message, 10000), &screen);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, &screen,
                         [=](QDBusPendingCallWatcher* call) {
            const QDBusPendingReply<> reply = *call;
            if (reply.isError()) {
                fmt::print(stderr, "Shutdown failed: {}\n", reply.error().message().toStdString());
                status->setText("Shutdown failed. Check the service journal and try again.");
                button->setEnabled(true);
            }
            call->deleteLater();
        });
    });
    screen.showFullScreen();
    button->setFocus();
    if (smoke) {
        QTimer::singleShot(0, &screen, [&] {
            button->click();
            app.exit(simulated && !button->isEnabled()
                     && status->text() == "Dry run: shutdown requested" ? 0 : 1);
        });
    }
    return app.exec();
}
