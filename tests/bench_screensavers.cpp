// Manual frame-time benchmark; run on the target Pi, not part of ctest:
//   QT_QPA_PLATFORM=offscreen ./build/screensaver-bench
#include "screensaver/mandelbrot.h"
#include "screensaver/registry.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include <fmt/format.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

using namespace pi_control::screensaver;
using namespace std::chrono_literals;

namespace {

void report(const std::string& label, std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const auto at = [&](double fraction) {
        return samples[std::min(samples.size() - 1, std::size_t(fraction * double(samples.size())))];
    };
    fmt::print("{:<44} median {:7.2f} ms  p95 {:7.2f} ms  max {:7.2f} ms\n", label, at(0.5), at(0.95),
               samples.back());
}

std::vector<double> time(int warmup, int frames, const std::function<void()>& frame) {
    for (int i = 0; i < warmup; ++i) frame();
    std::vector<double> samples;
    samples.reserve(std::size_t(frames));
    QElapsedTimer timer;
    for (int i = 0; i < frames; ++i) {
        timer.start();
        frame();
        samples.push_back(double(timer.nsecsElapsed()) / 1.0e6);
    }
    return samples;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    for (const auto& descriptor : builtinScreensavers()) {
        for (const QSize size : {QSize(800, 480), QSize(1920, 1080)}) {
            auto saver = descriptor.create();
            saver->start(size, 1);
            QImage image(size, QImage::Format_RGB32);
            const auto samples = time(30, 300, [&] {
                saver->advance(33ms);
                QPainter painter(&image);
                saver->paint(painter, image.rect());
            });
            report(fmt::format("{} {}x{}", descriptor.id.toStdString(), size.width(), size.height()), samples);
        }
    }

    // Deep zoom frames are the slowest and a short run never reaches them.
    const auto palette = makeMandelbrotPalette();
    QImage image(scaledToBudget(QSize(1920, 1080), 320 * 180), QImage::Format_RGB32);
    for (const auto& target : mandelbrotZoomTargets()) {
        for (const double scale : {1.5, 1e-4, 1e-8, target.floorScale}) {
            if (scale < target.floorScale) continue;
            const int iterations = mandelbrotIterationsForScale(scale);
            const auto samples = time(3, 30, [&] {
                renderMandelbrot(image, target.cx, target.cy, scale, iterations, palette, 0.0);
            });
            report(fmt::format("mandelbrot ({:.6f},{:.6f}) scale {:g}", target.cx, target.cy, scale), samples);
        }
    }
    return 0;
}
