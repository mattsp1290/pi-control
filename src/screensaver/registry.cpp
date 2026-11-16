#include "screensaver/registry.h"

namespace pi_control::screensaver {

const std::vector<ScreensaverDescriptor>& builtinScreensavers() {
    static const std::vector<ScreensaverDescriptor> screensavers{};
    return screensavers;
}

const ScreensaverDescriptor* findScreensaver(QStringView id) {
    for (const auto& descriptor : builtinScreensavers()) {
        if (descriptor.id == id) return &descriptor;
    }
    return nullptr;
}

} // namespace pi_control::screensaver
