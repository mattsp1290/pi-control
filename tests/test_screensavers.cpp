#include "screensaver/life.h"
#include "screensaver/mandelbrot.h"
#include "screensaver/registry.h"

#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

using namespace pi_control::screensaver;
using namespace std::chrono_literals;

namespace {

constexpr QRgb kUnpainted = 0xffff00ff;

QImage renderFrames(Screensaver& saver, QSize size, quint32 seed, int frames) {
    saver.start(size, seed);
    for (int i = 0; i < frames; ++i) saver.advance(33ms);
    QImage image(size, QImage::Format_RGB32);
    image.fill(kUnpainted);
    QPainter painter(&image);
    saver.paint(painter, image.rect());
    return image;
}

} // namespace

class ScreensaverTests : public QObject {
    Q_OBJECT

private slots:
    void scaledToBudgetKeepsAspectWithinBudget() {
        QCOMPARE(scaledToBudget(QSize(0, 0), 100), QSize());
        QCOMPARE(scaledToBudget(QSize(10, 10), 400), QSize(10, 10));
        const QSize scaled = scaledToBudget(QSize(1920, 1080), 57600);
        QVERIFY(scaled.width() * scaled.height() <= 57600);
        QVERIFY(scaled.width() >= 318 && scaled.height() >= 178);
        QCOMPARE(scaledToBudget(QSize(1, 100000), 10), QSize(1, 10));
    }

    void registryIdsAreUniqueAndWellFormed() {
        const QRegularExpression pattern("^[a-z][a-z0-9-]*$");
        QSet<QString> ids;
        for (const auto& descriptor : builtinScreensavers()) {
            QVERIFY2(pattern.match(descriptor.id).hasMatch(), qPrintable(descriptor.id));
            QVERIFY2(!ids.contains(descriptor.id), qPrintable(descriptor.id));
            QVERIFY(!descriptor.displayName.isEmpty());
            QVERIFY(descriptor.create);
            ids.insert(descriptor.id);
        }
    }

    void findScreensaverReturnsNullForUnknownId() {
        QCOMPARE(findScreensaver(u"no-such-screensaver"), nullptr);
        for (const auto& descriptor : builtinScreensavers()) {
            QCOMPARE(findScreensaver(descriptor.id), &descriptor);
        }
    }

    void everyScreensaverRendersAFrame() {
        for (const auto& descriptor : builtinScreensavers()) {
            auto saver = descriptor.create();
            QVERIFY(saver);
            const QImage image = renderFrames(*saver, QSize(320, 240), 1, 10);
            for (int y = 0; y < image.height(); ++y) {
                const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    QVERIFY2(line[x] != kUnpainted,
                             qPrintable(QString("%1 left (%2,%3) unpainted").arg(descriptor.id).arg(x).arg(y)));
                }
            }
        }
    }

    void everyScreensaverIsDeterministicForASeed() {
        for (const auto& descriptor : builtinScreensavers()) {
            auto first = descriptor.create();
            auto second = descriptor.create();
            const QImage a = renderFrames(*first, QSize(200, 120), 42, 45);
            const QImage b = renderFrames(*second, QSize(200, 120), 42, 45);
            QVERIFY2(a == b, qPrintable(descriptor.id));
        }
    }

    void lifeBlinkerHasPeriodTwo() {
        LifeGrid grid;
        grid.resize(5, 5);
        for (int x = 1; x <= 3; ++x) grid.set(x, 2, true);
        const quint64 start = grid.hash();
        grid.step();
        QVERIFY(grid.alive(2, 1) && grid.alive(2, 2) && grid.alive(2, 3));
        QVERIFY(!grid.alive(1, 2) && !grid.alive(3, 2));
        QVERIFY(grid.hash() != start);
        grid.step();
        QCOMPARE(grid.hash(), start);
        QCOMPARE(grid.population(), 3);
    }

    void lifeBlockIsStillAndAges() {
        LifeGrid grid;
        grid.resize(6, 6);
        for (const auto& [x, y] : {std::pair{2, 2}, {3, 2}, {2, 3}, {3, 3}}) grid.set(x, y, true);
        const quint64 start = grid.hash();
        grid.step();
        grid.step();
        QCOMPARE(grid.hash(), start);
        QCOMPARE(grid.age(2, 2), std::uint8_t(2));
    }

    void lifeGliderWrapsAcrossTheEdge() {
        LifeGrid grid;
        grid.resize(8, 8);
        // Glider heading down-right, placed against the right/bottom edges.
        const std::pair<int, int> glider[] = {{6, 5}, {7, 6}, {5, 7}, {6, 7}, {7, 7}};
        for (const auto& [x, y] : glider) grid.set(x, y, true);
        for (int i = 0; i < 4; ++i) grid.step();
        QCOMPARE(grid.population(), 5);
        for (const auto& [x, y] : glider) QVERIFY(grid.alive((x + 1) % 8, (y + 1) % 8));
    }

    void lifeStagnationDetectedWithin31Generations() {
        LifeGrid grid;
        grid.resize(6, 6);
        for (const auto& [x, y] : {std::pair{2, 2}, {3, 2}, {2, 3}, {3, 3}}) grid.set(x, y, true);
        LifeStagnationDetector detector;
        int generations = 0;
        bool stalled = false;
        while (!stalled && generations < 31) {
            grid.step();
            ++generations;
            stalled = detector.observe(grid.hash(), grid.population());
        }
        QVERIFY(stalled);
        QVERIFY(generations <= 31);
    }

    void lifeStagnationDetectsEmptyAndOldRuns() {
        LifeStagnationDetector detector;
        QVERIFY(detector.observe(1, 0));
        detector.reset();
        for (int i = 1; i < LifeStagnationDetector::kMaxGenerations; ++i) QVERIFY(!detector.observe(quint64(i), 10));
        QVERIFY(detector.observe(quint64(LifeStagnationDetector::kMaxGenerations), 10));
    }

    void mandelbrotEscapeValues() {
        QCOMPARE(mandelbrotEscapeValue(0, 0, 100), -1.0);
        QCOMPARE(mandelbrotEscapeValue(-1, 0, 100), -1.0);
        const double outside = mandelbrotEscapeValue(2, 2, 100);
        QVERIFY(outside >= 0.0 && outside < 2.0);
    }

    void mandelbrotEarlyOutsMatchReferenceLoop() {
        const auto reference = [](double x, double y, int maxIterations) {
            double zx = 0, zy = 0;
            for (int n = 0; n < maxIterations; ++n) {
                const double magnitude2 = zx * zx + zy * zy;
                if (magnitude2 > 65536.0) return std::max(0.0, n + 1.0 - std::log2(0.5 * std::log(magnitude2)));
                const double nextX = zx * zx - zy * zy + x;
                zy = 2 * zx * zy + y;
                zx = nextX;
            }
            return -1.0;
        };
        for (const auto& [x, y] : {std::pair{-0.75, 0.1}, {0.3, 0.5}, {-1.25, 0.05}, {-0.1, 0.65}, {0.26, 0.0}}) {
            QVERIFY2(std::abs(mandelbrotEscapeValue(x, y, 2500) - reference(x, y, 2500)) < 1e-9,
                     qPrintable(QString("%1,%2").arg(x).arg(y)));
        }
    }

    void mandelbrotZoomShrinksThenMovesToNextTarget() {
        MandelbrotZoom zoom;
        zoom.start(3);
        const ZoomTarget first = zoom.target();
        double previous = zoom.scale();
        int frames = 0;
        while (zoom.cycles() == 0 && frames < 100000) {
            zoom.advance(0.033);
            ++frames;
            if (zoom.cycles() == 0) {
                QVERIFY(zoom.scale() <= previous);
                QVERIFY(zoom.scale() >= first.floorScale);
            }
            previous = zoom.scale();
        }
        QCOMPARE(zoom.cycles(), 1);
        QCOMPARE(zoom.scale(), MandelbrotZoom::kStartScale);
        QVERIFY(zoom.target().cx != first.cx || zoom.target().cy != first.cy);
        QCOMPARE(zoom.brightness(), 1.0);
    }

    void everyZoomTargetHasDetailAtFloor() {
        const auto palette = makeMandelbrotPalette();
        for (const auto& target : mandelbrotZoomTargets()) {
            QImage image(160, 90, QImage::Format_RGB32);
            renderMandelbrot(image, target.cx, target.cy, target.floorScale,
                             mandelbrotIterationsForScale(target.floorScale), palette, 0.0);
            QSet<QRgb> colors;
            int escaped = 0;
            for (int y = 0; y < image.height(); ++y) {
                const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    colors.insert(line[x]);
                    if (qRgb(0, 0, 0) != line[x]) ++escaped;
                }
            }
            const double fraction = double(escaped) / (image.width() * image.height());
            const QString label = QString("(%1, %2) escaped %3 colors %4")
                                      .arg(target.cx, 0, 'g', 17).arg(target.cy, 0, 'g', 17)
                                      .arg(fraction).arg(colors.size());
            // Not mostly interior, and not a flat smooth-colour field: a view
            // zoomed past its coordinate precision collapses to a few colours.
            QVERIFY2(fraction >= 0.05, qPrintable(label));
            QVERIFY2(colors.size() >= 64, qPrintable(label));
        }
    }

    void everyScreensaverAcceptsZeroAndOddSizes() {
        for (const auto& descriptor : builtinScreensavers()) {
            auto saver = descriptor.create();
            saver->start(QSize(0, 0), 7);
            saver->advance(33ms);
            QImage empty(1, 1, QImage::Format_RGB32);
            {
                QPainter painter(&empty);
                saver->paint(painter, QRect());
            }
            for (const QSize size : {QSize(1, 1), QSize(1921, 1081), QSize(3, 700)}) {
                saver->resize(size);
                saver->advance(100ms);
                QImage image(size, QImage::Format_RGB32);
                QPainter painter(&image);
                saver->paint(painter, image.rect());
            }
        }
    }
};

QTEST_MAIN(ScreensaverTests)
#include "test_screensavers.moc"
