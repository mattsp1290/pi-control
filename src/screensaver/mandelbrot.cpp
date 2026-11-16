#include "screensaver/mandelbrot.h"

#include <QColor>
#include <QPainter>
#include <QThread>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace pi_control::screensaver {

namespace {

constexpr int kMaxPixels = 320 * 180;
constexpr double kEscapeRadiusSquared = 65536.0;
constexpr double kPeriodTolerance = 1e-14;
constexpr int kMinIterations = 100;
constexpr int kMaxIterations = 2500;
constexpr double kIterationsPerOctave = 60.0;
constexpr double kIterationsPerColorCycle = 64.0;

} // namespace

const std::vector<ZoomTarget>& mandelbrotZoomTargets() {
    // Each target has its own floor: imprecise coordinates go flat past their
    // precision (everyZoomTargetHasDetailAtFloor), and slowly escaping regions
    // exceed the frame budget on a Pi 4 (Seahorse Valley below ~3e-6).
    static const std::vector<ZoomTarget> targets{
        {-0.743643887037158704752191506114774, 0.131825904205311970493132056385139, 3e-6}, // Seahorse Valley
        {0.0, 1.0, 1e-12},                                                                   // Misiurewicz c = i
        {-0.10109636384562, 0.95628651080914, 1e-10},                                       // Misiurewicz M(3,1)
        {-0.77568377, 0.13646737, 1e-6},                                                    // Antenna spiral
    };
    return targets;
}

double mandelbrotEscapeValue(double x, double y, int maxIterations) {
    // Main cardioid and period-2 bulb never escape.
    const double xq = x - 0.25;
    const double q = xq * xq + y * y;
    if (q * (q + xq) <= 0.25 * y * y) return -1.0;
    if ((x + 1.0) * (x + 1.0) + y * y <= 0.0625) return -1.0;

    double zx = 0.0;
    double zy = 0.0;
    double savedX = 0.0;
    double savedY = 0.0;
    int checkpoint = 1;
    for (int n = 0; n < maxIterations; ++n) {
        const double zx2 = zx * zx;
        const double zy2 = zy * zy;
        const double magnitude2 = zx2 + zy2;
        if (magnitude2 > kEscapeRadiusSquared) {
            const double smooth = n + 1.0 - std::log2(0.5 * std::log(magnitude2));
            return std::max(0.0, smooth);
        }
        zy = 2.0 * zx * zy + y;
        zx = zx2 - zy2 + x;
        // An orbit that returns to an earlier point is periodic: interior.
        if (std::abs(zx - savedX) < kPeriodTolerance && std::abs(zy - savedY) < kPeriodTolerance) return -1.0;
        if (n + 1 == checkpoint) {
            savedX = zx;
            savedY = zy;
            checkpoint *= 2;
        }
    }
    return -1.0;
}

int mandelbrotIterationsForScale(double scale) {
    const double octaves = std::log2(MandelbrotZoom::kStartScale / std::max(scale, 1e-300));
    return std::clamp(int(kMinIterations + kIterationsPerOctave * std::max(0.0, octaves)), kMinIterations,
                      kMaxIterations);
}

MandelbrotPalette makeMandelbrotPalette() {
    const QColor stops[] = {QColor(0x0b, 0x1d, 0x51), QColor(0x2f, 0x7c, 0xd4), QColor(0xf2, 0xf6, 0xff),
                            QColor(0xff, 0xa2, 0x1f), QColor(0x5a, 0x12, 0x05), QColor(0x0b, 0x1d, 0x51)};
    MandelbrotPalette palette{};
    const int segments = int(std::size(stops)) - 1;
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const double position = double(i) / double(palette.size()) * segments;
        const int segment = std::min(int(position), segments - 1);
        palette[i] = lerpRgb(stops[segment], stops[segment + 1], position - segment);
    }
    return palette;
}

void renderMandelbrot(QImage& image, double cx, double cy, double scale, int maxIterations,
                      const MandelbrotPalette& palette, double paletteOffset) {
    if (image.isNull() || image.width() == 0 || image.height() == 0) return;
    const int width = image.width();
    const int height = image.height();
    const double pixel = 2.0 * scale / height;
    const int bandCount = std::max(1, std::min(height, QThread::idealThreadCount() * 2));
    std::vector<int> bands(static_cast<std::size_t>(bandCount));
    std::iota(bands.begin(), bands.end(), 0);
    // Detach once here: QImage::scanLine() detaches and is not thread-safe.
    uchar* const bits = image.bits();
    const qsizetype stride = image.bytesPerLine();
    QtConcurrent::blockingMap(bands, [&](int band) {
        const int firstRow = height * band / bandCount;
        const int lastRow = height * (band + 1) / bandCount;
        for (int row = firstRow; row < lastRow; ++row) {
            auto* line = reinterpret_cast<QRgb*>(bits + row * stride);
            const double y = cy - (row - height / 2.0 + 0.5) * pixel;
            for (int column = 0; column < width; ++column) {
                const double x = cx + (column - width / 2.0 + 0.5) * pixel;
                const double value = mandelbrotEscapeValue(x, y, maxIterations);
                if (value < 0.0) {
                    line[column] = qRgb(0, 0, 0);
                    continue;
                }
                const double cycle = value / kIterationsPerColorCycle + paletteOffset;
                const double fraction = cycle - std::floor(cycle);
                line[column] = palette[std::min(palette.size() - 1, std::size_t(fraction * palette.size()))];
            }
        }
    });
}

void MandelbrotZoom::start(quint32 seed) {
    const auto& targets = mandelbrotZoomTargets();
    order_.resize(targets.size());
    std::iota(order_.begin(), order_.end(), 0);
    std::shuffle(order_.begin(), order_.end(), std::mt19937(seed));
    position_ = 0;
    scale_ = kStartScale;
    fade_ = -1.0;
    cycles_ = 0;
}

const ZoomTarget& MandelbrotZoom::target() const {
    const auto& targets = mandelbrotZoomTargets();
    return order_.empty() ? targets.front() : targets[std::size_t(order_[position_])];
}

void MandelbrotZoom::advance(double seconds) {
    if (fade_ >= 0.0) {
        fade_ += seconds;
        if (fade_ >= kFadeSeconds) {
            fade_ = -1.0;
            scale_ = kStartScale;
            position_ = order_.empty() ? 0 : (position_ + 1) % order_.size();
            ++cycles_;
        }
        return;
    }
    scale_ *= std::exp(-kZoomRate * seconds);
    if (scale_ < target().floorScale) {
        scale_ = target().floorScale;
        fade_ = 0.0;
    }
}

double MandelbrotZoom::brightness() const {
    return fade_ < 0.0 ? 1.0 : std::max(0.0, 1.0 - fade_ / kFadeSeconds);
}

MandelbrotScreensaver::MandelbrotScreensaver() : RasterScreensaver(qRgb(0, 0, 0), true) {}

void MandelbrotScreensaver::start(QSize size, quint32 seed) {
    palette_ = makeMandelbrotPalette();
    zoom_.start(seed);
    resize(size);
}

void MandelbrotScreensaver::resize(QSize size) {
    setFrameSize(scaledToBudget(size, kMaxPixels));
}

void MandelbrotScreensaver::advance(std::chrono::milliseconds dt) {
    zoom_.advance(dt.count() / 1000.0);
    invalidate();
}

void MandelbrotScreensaver::render(QImage& frame) {
    const double scale = zoom_.scale();
    renderMandelbrot(frame, zoom_.target().cx, zoom_.target().cy, scale, mandelbrotIterationsForScale(scale), palette_,
                     0.15 * std::log(MandelbrotZoom::kStartScale / scale));
}

void MandelbrotScreensaver::paintOverlay(QPainter& painter, const QRect& target) {
    const double darkness = 1.0 - zoom_.brightness();
    if (darkness > 0.0) painter.fillRect(target, QColor(0, 0, 0, int(darkness * 255)));
}

} // namespace pi_control::screensaver
