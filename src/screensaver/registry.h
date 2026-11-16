#pragma once

#include "screensaver/screensaver.h"

#include <QString>
#include <QStringView>

#include <functional>
#include <memory>
#include <vector>

namespace pi_control::screensaver {

struct ScreensaverDescriptor {
    QString id;          // Stable lowercase id; also the --screensaver value.
    QString displayName; // Human-readable name for logs.
    std::function<std::unique_ptr<Screensaver>()> create;
};

// Every built-in screensaver. See docs/screensavers.md to add one.
const std::vector<ScreensaverDescriptor>& builtinScreensavers();

// The built-in descriptor with this id, or nullptr.
const ScreensaverDescriptor* findScreensaver(QStringView id);

} // namespace pi_control::screensaver
