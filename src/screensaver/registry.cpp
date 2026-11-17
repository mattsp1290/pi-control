#include "screensaver/registry.h"

#include "screensaver/life.h"
#include "screensaver/mandelbrot.h"
#include "screensaver/water.h"

namespace pi_control::screensaver {

const std::vector<ScreensaverDescriptor>& builtinScreensavers() {
    static const std::vector<ScreensaverDescriptor> screensavers{
        {"life", "Conway's Game of Life", [] { return std::make_unique<LifeScreensaver>(); }},
        {"mandelbrot", "Mandelbrot zoom", [] { return std::make_unique<MandelbrotScreensaver>(); }},
        {"water", "Water ripples", [] { return std::make_unique<WaterScreensaver>(); }},
    };
    return screensavers;
}

const ScreensaverDescriptor* defaultScreensaver() { return &builtinScreensavers().front(); }

ScreensaverChoice resolveScreensaver(const ScreensaverDescriptor* override, QStringView savedId) {
    if (override) return {override};
    if (savedId.isEmpty()) return {defaultScreensaver()};
    if (const auto* saved = findScreensaver(savedId)) return {saved};
    return {defaultScreensaver(), true};
}

const ScreensaverDescriptor* findScreensaver(QStringView id) {
    for (const auto& descriptor : builtinScreensavers()) {
        if (descriptor.id == id) return &descriptor;
    }
    return nullptr;
}

} // namespace pi_control::screensaver
