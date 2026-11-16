#include "screensaver/life.h"

#include <QColor>
#include <QPainter>

#include <algorithm>
#include <random>

namespace pi_control::screensaver {

namespace {

constexpr int kMaxColumns = 240;
constexpr int kMaxRows = 135;
constexpr int kMinCellEdge = 4;
constexpr double kDensity = 0.30;
constexpr std::chrono::milliseconds kGenerationInterval{100};
constexpr int kMaxGenerationsPerAdvance = 5;
constexpr QRgb kDeadColor = 0xff101820;

QRgb blend(QColor from, QColor to, double t) {
    return qRgb(int(from.red() + (to.red() - from.red()) * t),
                int(from.green() + (to.green() - from.green()) * t),
                int(from.blue() + (to.blue() - from.blue()) * t));
}

} // namespace

void LifeGrid::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    const auto cells = std::size_t(width_) * std::size_t(height_);
    cells_.assign(cells, 0);
    next_.assign(cells, 0);
    ages_.assign(cells, 0);
}

void LifeGrid::clear() {
    std::fill(cells_.begin(), cells_.end(), 0);
    std::fill(ages_.begin(), ages_.end(), 0);
}

void LifeGrid::randomize(quint32 seed, double density) {
    std::mt19937 random(seed);
    std::bernoulli_distribution live(density);
    for (auto& cell : cells_) cell = live(random) ? 1 : 0;
    std::fill(ages_.begin(), ages_.end(), 0);
}

int LifeGrid::index(int x, int y) const {
    x = ((x % width_) + width_) % width_;
    y = ((y % height_) + height_) % height_;
    return y * width_ + x;
}

void LifeGrid::set(int x, int y, bool alive) {
    if (cells_.empty()) return;
    const int i = index(x, y);
    cells_[i] = alive ? 1 : 0;
    ages_[i] = 0;
}

bool LifeGrid::alive(int x, int y) const {
    return !cells_.empty() && cells_[index(x, y)] != 0;
}

std::uint8_t LifeGrid::age(int x, int y) const {
    return cells_.empty() ? 0 : ages_[index(x, y)];
}

void LifeGrid::step() {
    if (cells_.empty()) return;
    for (int y = 0; y < height_; ++y) {
        const int up = (y + height_ - 1) % height_ * width_;
        const int row = y * width_;
        const int down = (y + 1) % height_ * width_;
        for (int x = 0; x < width_; ++x) {
            const int left = (x + width_ - 1) % width_;
            const int right = (x + 1) % width_;
            const int neighbours = cells_[up + left] + cells_[up + x] + cells_[up + right]
                                 + cells_[row + left] + cells_[row + right]
                                 + cells_[down + left] + cells_[down + x] + cells_[down + right];
            const bool live = cells_[row + x] != 0;
            next_[row + x] = (neighbours == 3 || (live && neighbours == 2)) ? 1 : 0;
        }
    }
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        if (next_[i] && cells_[i]) ages_[i] = std::uint8_t(std::min(255, ages_[i] + 1));
        else ages_[i] = 0;
    }
    cells_.swap(next_);
}

int LifeGrid::population() const {
    return int(std::count(cells_.begin(), cells_.end(), std::uint8_t(1)));
}

quint64 LifeGrid::hash() const {
    quint64 hash = 1469598103934665603ULL; // FNV-1a
    for (const auto cell : cells_) {
        hash ^= cell;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void LifeStagnationDetector::reset() {
    recorded_ = 0;
    generations_ = 0;
}

bool LifeStagnationDetector::observe(quint64 hash, int population) {
    ++generations_;
    if (population == 0 || generations_ >= kMaxGenerations) return true;
    const int known = std::min(recorded_, kHistory);
    for (int i = 0; i < known; ++i) {
        if (history_[i] == hash) return true;
    }
    history_[recorded_ % kHistory] = hash;
    ++recorded_;
    return false;
}

void LifeScreensaver::start(QSize size, quint32 seed) {
    seed_ = seed;
    resets_ = 0;
    const QColor young(0x5f, 0xf4, 0xff);
    const QColor old(0x0e, 0x7c, 0x86);
    for (int age = 0; age < 256; ++age) {
        palette_[age] = blend(young, old, std::min(age, 32) / 32.0);
    }
    resize(size);
}

void LifeScreensaver::resize(QSize size) {
    int columns = 0;
    int rows = 0;
    if (!size.isEmpty()) {
        const int edge = std::max(kMinCellEdge, size.width() / kMaxColumns);
        columns = std::clamp(size.width() / edge, 1, kMaxColumns);
        rows = std::clamp(size.height() / edge, 1, kMaxRows);
    }
    grid_.resize(columns, rows);
    image_ = columns > 0 ? QImage(columns, rows, QImage::Format_RGB32) : QImage();
    pending_ = {};
    reseed();
}

void LifeScreensaver::reseed() {
    grid_.randomize(seed_ ^ (resets_ * 0x9e3779b9u), kDensity);
    ++resets_;
    detector_.reset();
    dirty_ = true;
}

void LifeScreensaver::advance(std::chrono::milliseconds dt) {
    pending_ += dt;
    for (int i = 0; i < kMaxGenerationsPerAdvance && pending_ >= kGenerationInterval; ++i) {
        pending_ -= kGenerationInterval;
        grid_.step();
        dirty_ = true;
        if (detector_.observe(grid_.hash(), grid_.population())) reseed();
    }
    pending_ = std::min(pending_, kGenerationInterval);
}

void LifeScreensaver::render() {
    for (int y = 0; y < grid_.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image_.scanLine(y));
        for (int x = 0; x < grid_.width(); ++x) {
            line[x] = grid_.alive(x, y) ? palette_[grid_.age(x, y)] : kDeadColor;
        }
    }
    dirty_ = false;
}

void LifeScreensaver::paint(QPainter& painter, const QRect& target) {
    if (target.isEmpty()) return;
    if (image_.isNull()) {
        painter.fillRect(target, QColor::fromRgb(kDeadColor));
        return;
    }
    if (dirty_) render();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, image_);
}

} // namespace pi_control::screensaver
