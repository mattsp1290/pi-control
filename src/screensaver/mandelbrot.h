#pragma once

#include "screensaver/screensaver.h"

#include <QImage>

#include <array>
#include <vector>

namespace pi_control::screensaver {

struct ZoomTarget {
    double cx;
    double cy;
    double floorScale; // Deepest half-height that still shows detail.
};

const std::vector<ZoomTarget>& mandelbrotZoomTargets();

// Smooth escape count for c = x + iy, or -1 when c does not escape.
double mandelbrotEscapeValue(double x, double y, int maxIterations);

// Iteration limit that keeps detail visible at this view half-height.
int mandelbrotIterationsForScale(double scale);

using MandelbrotPalette = std::array<QRgb, 1024>;
MandelbrotPalette makeMandelbrotPalette();

// Renders the view centred on (cx, cy) with half-height scale into image,
// using every core.
void renderMandelbrot(QImage& image, double cx, double cy, double scale, int maxIterations,
                      const MandelbrotPalette& palette, double paletteOffset);

// Zooms exponentially toward a sequence of targets, fading out at each floor.
class MandelbrotZoom {
public:
    static constexpr double kStartScale = 1.5;
    static constexpr double kZoomRate = 0.35;   // Natural-log units per second.
    static constexpr double kFadeSeconds = 0.5;

    void start(quint32 seed);
    void advance(double seconds);
    const ZoomTarget& target() const;
    double scale() const { return scale_; }
    double brightness() const;
    int cycles() const { return cycles_; }

private:
    std::vector<int> order_;
    std::size_t position_ = 0;
    double scale_ = kStartScale;
    double fade_ = -1.0; // Seconds into the fade-out, or negative when zooming.
    int cycles_ = 0;
};

class MandelbrotScreensaver final : public Screensaver {
public:
    void start(QSize size, quint32 seed) override;
    void resize(QSize size) override;
    void advance(std::chrono::milliseconds dt) override;
    void paint(QPainter& painter, const QRect& target) override;

private:
    MandelbrotZoom zoom_;
    MandelbrotPalette palette_{};
    QImage image_;
    bool dirty_ = true;
};

} // namespace pi_control::screensaver
