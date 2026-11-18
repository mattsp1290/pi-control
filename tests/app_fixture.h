#pragma once

#include "app/app_context.h"
#include "app/app_shell.h"
#include "app/settings_store.h"
#include "screensaver/screensaver_controller.h"
#include "system/system_stats.h"
#include "update/updater.h"
#include "wifi/fake_wifi_backend.h"

#include <QTabWidget>
#include <QTemporaryDir>

#include <memory>

// A full shell at 800x480 with every service the pages need.
struct AppFixture {
    // Services a test may replace before the pages are built.
    struct Services {
        std::unique_ptr<pi_control::system::SystemStatsSampler> stats;
        std::unique_ptr<pi_control::wifi::WifiBackend> wifi;
        QString settingsFile; // Empty: a fresh file in a temporary directory.
        std::unique_ptr<pi_control::update::Updater> updater;
    };

    explicit AppFixture(pi_control::app::PowerOptions power = {.dryRun = true},
                        pi_control::screensaver::ScreensaverControllerConfig saverConfig = {},
                        Services services = {}) {
        stats = services.stats ? std::move(services.stats)
                               : std::make_unique<pi_control::system::SystemStatsSampler>();
        wifi = services.wifi ? std::move(services.wifi) : std::unique_ptr<pi_control::wifi::WifiBackend>(pi_control::wifi::FakeWifiBackend::withSampleNetworks());
        settings = std::make_unique<pi_control::app::SettingsStore>(
            services.settingsFile.isEmpty() ? settingsDir.filePath("settings.ini") : services.settingsFile);
        updater = services.updater ? std::move(services.updater)
                                   : std::make_unique<pi_control::update::Updater>(pi_control::update::UpdaterOptions{
                                         .current = {0, 2, 0}, .releaseBase = QUrl("file:///nonexistent"), .dryRun = true});
        shell = std::make_unique<pi_control::app::AppShell>();
        shell->resize(800, 480);
        controller = std::make_unique<pi_control::screensaver::ScreensaverController>(shell->screensaverView(),
                                                                                       std::move(saverConfig));
        context = std::make_unique<pi_control::app::AppContext>(
            pi_control::app::AppContext{*shell, *controller, power, *stats, *wifi, *settings, *updater});
        shell->buildTabs(*context);
    }
    ~AppFixture() {
        controller.reset();
        shell.reset();
    }

    static Services withSettingsFile(QString path) {
        Services services;
        services.settingsFile = std::move(path);
        return services;
    }

    pi_control::wifi::FakeWifiBackend& fakeWifi() const {
        return *qobject_cast<pi_control::wifi::FakeWifiBackend*>(wifi.get());
    }
    template <typename Page> Page* page(QStringView id) const { return qobject_cast<Page*>(shell->page(id)); }
    void showTab(QStringView id) const { shell->tabWidget()->setCurrentWidget(shell->page(id)); }

    std::unique_ptr<pi_control::system::SystemStatsSampler> stats;
    std::unique_ptr<pi_control::wifi::WifiBackend> wifi;
    QTemporaryDir settingsDir;
    std::unique_ptr<pi_control::app::SettingsStore> settings;
    std::unique_ptr<pi_control::update::Updater> updater;
    std::unique_ptr<pi_control::app::AppShell> shell;
    std::unique_ptr<pi_control::screensaver::ScreensaverController> controller;
    std::unique_ptr<pi_control::app::AppContext> context;
};
