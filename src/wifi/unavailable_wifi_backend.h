#pragma once

#include "wifi/wifi_backend.h"

namespace pi_control::wifi {

class UnavailableWifiBackend final : public WifiBackend {
public:
    WifiStatus status() const override {
        return {.state = WifiStatus::State::Unavailable, .detail = "Not implemented yet"};
    }
};

} // namespace pi_control::wifi
