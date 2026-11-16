#pragma once

#include <QRect>
#include <QSize>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cmath>

class QPainter;

namespace pi_control::screensaver {

// One animated screensaver. The view owns time, painting cadence and input;
// an implementation only simulates and paints. All randomness must derive
// from the seed passed to start() so a run is reproducible.
class Screensaver {
public:
    virtual ~Screensaver() = default;
    // Called once per activation before the first advance()/paint().
    virtual void start(QSize size, quint32 seed) = 0;
    // Called when the view size changes while active; never with the size
    // already passed to start() or the previous resize().
    virtual void resize(QSize size) = 0;
    // Advance simulation time; dt is already clamped by the view.
    virtual void advance(std::chrono::milliseconds dt) = 0;
    // Paint every pixel of target. Must not change simulation state.
    virtual void paint(QPainter& painter, const QRect& target) = 0;
};

// Largest size with the aspect ratio of view whose area is at most maxPixels.
// Both sides are at least 1, or the result is empty for an empty view.
inline QSize scaledToBudget(QSize view, int maxPixels) {
    if (view.isEmpty() || maxPixels <= 0) return {};
    const double area = double(view.width()) * view.height();
    if (area <= maxPixels) return view;
    const double factor = std::sqrt(maxPixels / area);
    int width = std::max(1, int(view.width() * factor));
    int height = std::max(1, int(view.height() * factor));
    while (double(width) * height > maxPixels) {
        if (width >= height && width > 1) --width;
        else if (height > 1) --height;
        else break;
    }
    return {width, height};
}

} // namespace pi_control::screensaver
