#pragma once

#include "screensaver/raster_screensaver.h"

#include <array>
#include <cstdint>
#include <vector>

namespace pi_control::screensaver {

// Toroidal Conway's Game of Life (B3/S23) with per-cell age.
class LifeGrid {
public:
    void resize(int width, int height);
    int width() const { return width_; }
    int height() const { return height_; }
    void clear();
    void randomize(quint32 seed, double density);
    void set(int x, int y, bool alive);
    bool alive(int x, int y) const;
    std::uint8_t age(int x, int y) const;
    void step();
    int population() const;
    quint64 hash() const;

private:
    int index(int x, int y) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> cells_;
    std::vector<std::uint8_t> next_;
    std::vector<std::uint8_t> ages_;
};

// Decides when a Life run has stalled and must be reseeded.
class LifeStagnationDetector {
public:
    static constexpr int kHistory = 30;
    static constexpr int kMaxGenerations = 2000;

    void reset();
    // Records one generation. Returns true when the run should be reseeded:
    // empty, repeating a state from the last kHistory generations, or too old.
    bool observe(quint64 hash, int population);

private:
    std::array<quint64, kHistory> history_{};
    int recorded_ = 0;
    int generations_ = 0;
};

class LifeScreensaver final : public RasterScreensaver {
public:
    LifeScreensaver();
    void start(QSize size, quint32 seed) override;
    void resize(QSize size) override;
    void advance(std::chrono::milliseconds dt) override;

private:
    void reseed();
    void render(QImage& frame) override;

    LifeGrid grid_;
    LifeStagnationDetector detector_;
    quint32 seed_ = 0;
    quint32 resets_ = 0;
    FixedStep<std::chrono::milliseconds> clock_;
    std::array<QRgb, 256> palette_{};
};

} // namespace pi_control::screensaver
