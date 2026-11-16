#include "screensaver/water.h"

#include <QColor>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pi_control::screensaver {

namespace {

constexpr int kMaxPixels = 400 * 225;
constexpr double kStepSeconds = 1.0 / 60.0;
constexpr int kMaxStepsPerAdvance = 6;
constexpr double kDropsPerSecond = 3.0;
constexpr float kRefraction = 8.0f;

float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// One octave of smooth value noise in [0, 1] with lattice spacing cell.
std::vector<float> valueNoise(QSize size, int cell, std::mt19937& random) {
    const int columns = size.width() / cell + 2;
    const int rows = size.height() / cell + 2;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::vector<float> lattice(std::size_t(columns) * std::size_t(rows));
    for (auto& value : lattice) value = unit(random);
    const auto at = [&](int i, int j) { return lattice[std::size_t(j * columns + i)]; };
    std::vector<float> noise(std::size_t(size.width()) * std::size_t(size.height()));
    for (int y = 0; y < size.height(); ++y) {
        const int cy = y / cell;
        const float fy = smoothstep(float(y % cell) / cell);
        for (int x = 0; x < size.width(); ++x) {
            const int cx = x / cell;
            const float fx = smoothstep(float(x % cell) / cell);
            const float top = at(cx, cy) + (at(cx + 1, cy) - at(cx, cy)) * fx;
            const float bottom = at(cx, cy + 1) + (at(cx + 1, cy + 1) - at(cx, cy + 1)) * fx;
            noise[std::size_t(y * size.width() + x)] = top + (bottom - top) * fy;
        }
    }
    return noise;
}

// Soft sand and teal mottling: the pool floor seen through the water.
QImage makePoolFloor(QSize size, std::mt19937& random) {
    const auto coarse = valueNoise(size, 56, random);
    const auto fine = valueNoise(size, 14, random);
    const QColor sand(0xd8, 0xc8, 0x9a);
    const QColor teal(0x2a, 0x8f, 0x9a);
    QImage floor(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(floor.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const auto i = std::size_t(y * size.width() + x);
            const float t = std::clamp(0.7f * coarse[i] + 0.3f * fine[i], 0.0f, 1.0f);
            line[x] = qRgb(int(teal.red() + (sand.red() - teal.red()) * t),
                           int(teal.green() + (sand.green() - teal.green()) * t),
                           int(teal.blue() + (sand.blue() - teal.blue()) * t));
        }
    }
    return floor;
}

} // namespace

void WaveField::resize(int width, int height) {
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    previous_.assign(std::size_t(width_) * std::size_t(height_), 0.0f);
    current_ = previous_;
}

void WaveField::step() {
    if (current_.empty()) return;
    // previous_ becomes the next state: next = neighbours / 2 - previous.
    for (int y = 0; y < height_; ++y) {
        const int up = std::max(0, y - 1) * width_;
        const int down = std::min(height_ - 1, y + 1) * width_;
        const int row = y * width_;
        for (int x = 0; x < width_; ++x) {
            const int left = std::max(0, x - 1);
            const int right = std::min(width_ - 1, x + 1);
            const float neighbours = current_[row + left] + current_[row + right] + current_[up + x] + current_[down + x];
            previous_[row + x] = (neighbours * 0.5f - previous_[row + x]) * kDamping;
        }
    }
    previous_.swap(current_);
}

void WaveField::drop(int x, int y, int radius, float strength) {
    if (current_.empty() || radius <= 0) return;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int px = x + dx;
            const int py = y + dy;
            if (px < 0 || py < 0 || px >= width_ || py >= height_) continue;
            const float distance = std::sqrt(float(dx * dx + dy * dy));
            if (distance >= radius) continue;
            current_[std::size_t(py * width_ + px)] +=
                strength * 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * distance / radius));
        }
    }
}

float WaveField::height(int x, int y) const {
    if (current_.empty()) return 0.0f;
    return current_[std::size_t(std::clamp(y, 0, height_ - 1) * width_ + std::clamp(x, 0, width_ - 1))];
}

float WaveField::energy() const {
    float sum = 0.0f;
    for (const float value : current_) sum += value * value;
    return sum;
}

void WaterScreensaver::start(QSize size, quint32 seed) {
    random_.seed(seed);
    resize(size);
}

void WaterScreensaver::resize(QSize size) {
    const QSize internal = scaledToBudget(size, kMaxPixels);
    field_.resize(internal.width(), internal.height());
    floor_ = internal.isEmpty() ? QImage() : makePoolFloor(internal, random_);
    image_ = internal.isEmpty() ? QImage() : QImage(internal, QImage::Format_RGB32);
    pendingSeconds_ = 0.0;
    scheduleNextDrop();
    dirty_ = true;
}

void WaterScreensaver::scheduleNextDrop() {
    untilDrop_ = std::exponential_distribution<double>(kDropsPerSecond)(random_);
}

void WaterScreensaver::advance(std::chrono::milliseconds dt) {
    if (field_.width() == 0) return;
    pendingSeconds_ += dt.count() / 1000.0;
    for (int i = 0; i < kMaxStepsPerAdvance && pendingSeconds_ >= kStepSeconds; ++i) {
        pendingSeconds_ -= kStepSeconds;
        untilDrop_ -= kStepSeconds;
        while (untilDrop_ <= 0.0) {
            std::uniform_int_distribution<int> x(0, field_.width() - 1);
            std::uniform_int_distribution<int> y(0, field_.height() - 1);
            std::uniform_int_distribution<int> radius(2, 6);
            std::uniform_real_distribution<float> strength(0.5f, 1.5f);
            field_.drop(x(random_), y(random_), radius(random_), strength(random_));
            untilDrop_ += std::exponential_distribution<double>(kDropsPerSecond)(random_);
        }
        field_.step();
        dirty_ = true;
    }
    pendingSeconds_ = std::min(pendingSeconds_, kStepSeconds);
}

void WaterScreensaver::render() {
    const int width = field_.width();
    const int height = field_.height();
    // Light from the upper left; the highlight is the Blinn-Phong term of the
    // surface normal (-dx, -dy, 1) against this direction.
    const float lx = -0.35f, ly = -0.35f, lz = 0.87f;
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image_.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const float dx = field_.height(x + 1, y) - field_.height(x - 1, y);
            const float dy = field_.height(x, y + 1) - field_.height(x, y - 1);
            const int sx = std::clamp(int(float(x) + dx * kRefraction), 0, width - 1);
            const int sy = std::clamp(int(float(y) + dy * kRefraction), 0, height - 1);
            const QRgb base = reinterpret_cast<const QRgb*>(floor_.constScanLine(sy))[sx];
            const float inverseLength = 1.0f / std::sqrt(dx * dx + dy * dy + 1.0f);
            const float facing = std::max(0.0f, (-dx * lx - dy * ly + lz) * inverseLength);
            const float highlight = std::pow(facing, 32.0f) * 160.0f;
            const auto shade = [&](int channel, int deep) {
                return std::clamp(int(channel * 0.65f + deep * 0.35f + highlight), 0, 255);
            };
            line[x] = qRgb(shade(qRed(base), 0x06), shade(qGreen(base), 0x3a), shade(qBlue(base), 0x6e));
        }
    }
    dirty_ = false;
}

void WaterScreensaver::paint(QPainter& painter, const QRect& target) {
    if (target.isEmpty()) return;
    if (image_.isNull()) {
        painter.fillRect(target, QColor(0x06, 0x3a, 0x6e));
        return;
    }
    if (dirty_) render();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, image_);
}

} // namespace pi_control::screensaver
