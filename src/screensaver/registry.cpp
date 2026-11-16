#include "screensaver/registry.h"

#include "screensaver/life.h"
#include "screensaver/mandelbrot.h"

namespace pi_control::screensaver {

const std::vector<ScreensaverDescriptor>& builtinScreensavers() {
    static const std::vector<ScreensaverDescriptor> screensavers{
        {"life", "Conway's Game of Life", [] { return std::make_unique<LifeScreensaver>(); }},
        {"mandelbrot", "Mandelbrot zoom", [] { return std::make_unique<MandelbrotScreensaver>(); }},
    };
    return screensavers;
}

const ScreensaverDescriptor* findScreensaver(QStringView id) {
    for (const auto& descriptor : builtinScreensavers()) {
        if (descriptor.id == id) return &descriptor;
    }
    return nullptr;
}

} // namespace pi_control::screensaver
