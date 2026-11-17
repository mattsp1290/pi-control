#include <QApplication>
#include <QCommandLineParser>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>

#include <fmt/base.h>

#include "app/app_context.h"
#include "app/app_shell.h"
#include "app/power_panel.h"
#include "app/settings_page.h"
#include "screensaver/registry.h"
#include "screensaver/screensaver_controller.h"

using namespace pi_control;
using namespace pi_control::screensaver;

namespace {

bool hasExpectedTabs(const QTabWidget& tabs) {
    QStringList titles;
    for (int i = 0; i < tabs.count(); ++i) titles << tabs.tabText(i);
    return titles == QStringList{"Home", "Example", "Settings"};
}

// Switches to Settings and presses Shut down the way a tap would, without QTest.
app::PowerPanel* openPowerPanel(app::AppShell& shell) {
    auto* settings = qobject_cast<app::SettingsPage*>(shell.page(u"settings"));
    if (!settings) return nullptr;
    shell.tabWidget()->setCurrentWidget(settings);
    return settings->powerPanel();
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("pi-control");
    QCommandLineParser parser;
    parser.setApplicationDescription("Raspberry Pi control screen");
    parser.addHelpOption();
    parser.addOption({"dry-run", "Simulate shutdown without powering off."});
    parser.addOption({"smoke-test", "Exercise the real button safely and exit."});
    parser.addOption({"smoke-test-failure", "Safely exercise a disconnected shutdown bus and exit."});
    QStringList screensaverIds;
    for (const auto& descriptor : builtinScreensavers()) screensaverIds << descriptor.id;
    const QCommandLineOption screensaverOption(
        "screensaver", "Use only this screensaver (still starts after 15 s idle): " + screensaverIds.join(", ") + ".",
        "id");
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
    const app::PowerOptions power{.dryRun = smoke || parser.isSet("dry-run"), .failureTest = failureTest};

    app::AppShell shell;
    ScreensaverController saverController(shell.screensaverView(), std::move(saverConfig));
    const app::AppContext context{shell, saverController, power};
    shell.buildTabs(context);
    shell.showFullScreen();

    if (smoke) {
        QTimer::singleShot(0, &shell, [&] {
            auto* panel = openPowerPanel(shell);
            if (!panel || !hasExpectedTabs(*shell.tabWidget())) {
                fmt::print(stderr, "Smoke test: unexpected tabs\n");
                app.exit(1);
                return;
            }
            QPushButton* button = panel->shutdownButton();
            if (failureTest) {
                QObject::connect(panel, &app::PowerPanel::shutdownFailed, &app, [&app, panel] {
                    app.exit(panel->shutdownButton()->isEnabled()
                             && panel->statusLabel()->text().startsWith("Shutdown failed") ? 0 : 1);
                });
                button->click();
                return; // Completion is checked by the asynchronous failure signal.
            }
            bool simulated = false;
            const auto observed = QObject::connect(panel, &app::PowerPanel::dryRunCompleted, &app,
                                                   [&simulated] { simulated = true; });
            button->click();
            QObject::disconnect(observed);
            app.exit(simulated && !button->isEnabled()
                     && panel->statusLabel()->text() == "Dry run: shutdown requested" ? 0 : 1);
        });
    }
    return app.exec();
}
