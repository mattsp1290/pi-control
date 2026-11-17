#pragma once

#include "app/power_panel.h"

namespace pi_control::screensaver {
class ScreensaverController;
}
namespace pi_control::system {
class SystemStatsSampler;
}
namespace pi_control::wifi {
class WifiBackend;
}

namespace pi_control::app {

class AppShell;

// Non-owning references to the services pages share. main() owns them all and
// keeps them alive for the life of the shell.
struct AppContext {
    AppShell& shell;
    screensaver::ScreensaverController& screensaver;
    PowerOptions power;
    system::SystemStatsSampler& stats;
    wifi::WifiBackend& wifi;
};

} // namespace pi_control::app
