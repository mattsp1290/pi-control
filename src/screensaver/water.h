#pragma once

#include "screensaver/raster_screensaver.h"

#include <QImage>

#include <random>
#include <vector>

namespace pi_control::screensaver {

// Damped 2-D wave equation on a height field with reflecting edges.
class WaveField {
public:
    static constexpr float kDamping = 0.985f;

    void resize(int width, int height);
    int width() const { return width_; }
    int height() const { return height_; }
    void step();
    // Adds a cosine-shaped bump centred on (x, y).
    void drop(int x, int y, int radius, float strength);
    float height(int x, int y) const;
    float energy() const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<float> previous_;
    std::vector<float> current_;
};

// Rain falling on water seen from above: ripples refract a pool floor.
class WaterScreensaver final : public RasterScreensaver {
public:
    WaterScreensaver();
    void start(QSize size, quint32 seed) override;
    void resize(QSize size) override;
    void advance(std::chrono::milliseconds dt) override;

private:
    using Seconds = std::chrono::duration<double>;

    void scheduleNextDrop();
    void render(QImage& frame) override;

    WaveField field_;
    std::mt19937 random_;
    QImage floor_;
    FixedStep<Seconds> clock_;
    double untilDrop_ = 0.0;
};

} // namespace pi_control::screensaver
