#include "wifi/wifi_backend.h"

namespace pi_control::wifi {

const char* toString(WifiError error) {
    switch (error) {
    case WifiError::None: return "None";
    case WifiError::WrongPassword: return "WrongPassword";
    case WifiError::NotPermitted: return "NotPermitted";
    case WifiError::NotFound: return "NotFound";
    case WifiError::Timeout: return "Timeout";
    case WifiError::Unavailable: return "Unavailable";
    case WifiError::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace pi_control::wifi
