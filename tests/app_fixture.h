#pragma once

#include "app/app_context.h"
#include "app/app_shell.h"
#include "screensaver/screensaver_controller.h"
#include "system/system_stats.h"
#include "wifi/unavailable_wifi_backend.h"

#include <QTabWidget>

#include <memory>

// A full shell at 800x480 with every service the pages need.
struct AppFixture {
    // Services a test may replace before the pages are built.
    struct Services {
        std::unique_ptr<pi_control::system::SystemStatsSampler> stats;
        std::unique_ptr<pi_control::wifi::WifiBackend> wifi;
    };

    explicit AppFixture(pi_control::app::PowerOptions power = {.dryRun = true},
                        pi_control::screensaver::ScreensaverControllerConfig saverConfig = {},
                        Services services = {}) {
        stats = services.stats ? std::move(services.stats)
                               : std::make_unique<pi_control::system::SystemStatsSampler>();
        wifi = services.wifi ? std::move(services.wifi) : std::make_unique<pi_control::wifi::UnavailableWifiBackend>();
        shell = std::make_unique<pi_control::app::AppShell>();
        shell->resize(800, 480);
        controller = std::make_unique<pi_control::screensaver::ScreensaverController>(shell->screensaverView(),
                                                                                       std::move(saverConfig));
        context = std::make_unique<pi_control::app::AppContext>(
            pi_control::app::AppContext{*shell, *controller, power, *stats, *wifi});
        shell->buildTabs(*context);
    }
    ~AppFixture() {
        controller.reset();
        shell.reset();
    }

    template <typename Page> Page* page(QStringView id) const { return qobject_cast<Page*>(shell->page(id)); }
    void showTab(QStringView id) const { shell->tabWidget()->setCurrentWidget(shell->page(id)); }

    std::unique_ptr<pi_control::system::SystemStatsSampler> stats;
    std::unique_ptr<pi_control::wifi::WifiBackend> wifi;
    std::unique_ptr<pi_control::app::AppShell> shell;
    std::unique_ptr<pi_control::screensaver::ScreensaverController> controller;
    std::unique_ptr<pi_control::app::AppContext> context;
};
