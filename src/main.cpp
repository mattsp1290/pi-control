#include <QApplication>
#include <QCommandLineParser>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>

#include <fmt/base.h>

#include "app/app_context.h"
#include "app/app_shell.h"
#include "app/home_page.h"
#include "app/power_panel.h"
#include "app/settings_page.h"
#include "app/settings_store.h"
#include "screensaver/registry.h"
#include "screensaver/screensaver_controller.h"
#include "system/system_stats.h"
#include "wifi/fake_wifi_backend.h"
#include "wifi/nm_wifi_backend.h"

#include <QDBusConnection>
#include <QTemporaryDir>

#include <memory>

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

// Prints every Home value once, after an optional scan. Read-only: it never
// connects. Exit code 0 unless the requested scan failed.
void runStatusReport(QApplication& app, app::AppShell& shell, wifi::WifiBackend& backend, bool scan) {
    auto* home = qobject_cast<app::HomePage*>(shell.page(u"home"));
    home->refresh();
    auto report = [&app, home](int exitCode) {
        home->refresh(); // The second sample has a CPU value.
        for (const QString& line : home->reportLines()) fmt::print("{}\n", line.toStdString());
        app.exit(exitCode);
    };
    // Give the backend time to read NetworkManager's state.
    QTimer::singleShot(2000, &shell, [&app, &backend, scan, report] {
        if (!scan) {
            report(0);
            return;
        }
        QTimer::singleShot(20000, &app, [report] {
            fmt::print("Wi‑Fi scan: no result within 20 s\n");
            report(1);
        });
        QObject::connect(&backend, &wifi::WifiBackend::scanFinished, &app, [&backend, report](wifi::WifiError error) {
            fmt::print("Wi‑Fi scan: {} networks={}\n", wifi::toString(error), backend.networks().size());
            report(error == wifi::WifiError::None ? 0 : 1);
        });
        backend.requestScan();
    });
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
        "screensaver",
        "Use this screensaver for this run without saving it (still starts after 15 s idle): "
            + screensaverIds.join(", ") + ".",
        "id");
    parser.addOption(screensaverOption);
    parser.addOption({"fake-wifi", "Use canned Wi‑Fi networks instead of NetworkManager."});
    QCommandLineOption statusReportOption("status-report");
    statusReportOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(statusReportOption);
    QCommandLineOption settingsFileOption("settings-file", "Settings file to use.", "path");
    settingsFileOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(settingsFileOption);
    QCommandLineOption scanOption("scan");
    scanOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(scanOption);
    parser.process(app);
    const ScreensaverDescriptor* cliScreensaver = nullptr;
    if (parser.isSet(screensaverOption)) {
        const QString id = parser.value(screensaverOption);
        cliScreensaver = findScreensaver(id);
        if (!cliScreensaver) {
            fmt::print(stderr, "Unknown screensaver: {}\n", id.toStdString());
            return 2;
        }
    }
    const bool failureTest = parser.isSet("smoke-test-failure");
    const bool smoke = parser.isSet("smoke-test") || failureTest;

    // Smoke runs never touch the developer's own settings.
    QTemporaryDir smokeSettings;
    const QString settingsPath = parser.isSet(settingsFileOption) ? parser.value(settingsFileOption)
                                 : smoke                          ? smokeSettings.filePath("settings.ini")
                                                                  : app::SettingsStore::defaultPath();
    app::SettingsStore settingsStore(settingsPath);
    ScreensaverControllerConfig saverConfig;
    if (cliScreensaver) {
        saverConfig.screensaver = cliScreensaver;
    } else if (const QString saved = settingsStore.screensaverId(); !saved.isEmpty()) {
        saverConfig.screensaver = findScreensaver(saved);
        if (!saverConfig.screensaver) {
            saverConfig.screensaver = defaultScreensaver();
            fmt::print(stderr, "Screensaver: unknown saved id {}; using {}\n", saved.toStdString(),
                       saverConfig.screensaver->id.toStdString());
        }
    }
    fmt::print("Screensaver selection: {}\n", saverConfig.screensaver->id.toStdString());
    const app::PowerOptions power{.dryRun = smoke || parser.isSet("dry-run"), .failureTest = failureTest};

    system::SystemStatsSampler stats;
    std::unique_ptr<wifi::WifiBackend> wifiBackend;
    if (smoke || parser.isSet("fake-wifi")) {
        wifiBackend.reset(wifi::FakeWifiBackend::withSampleNetworks());
    } else {
        wifiBackend = std::make_unique<wifi::NmWifiBackend>(QDBusConnection::systemBus());
    }
    app::AppShell shell;
    ScreensaverController saverController(shell.screensaverView(), std::move(saverConfig));
    const app::AppContext context{shell, saverController, power, stats, *wifiBackend, settingsStore};
    shell.buildTabs(context);

    if (parser.isSet(statusReportOption)) {
        runStatusReport(app, shell, *wifiBackend, parser.isSet(scanOption));
        return app.exec();
    }
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
