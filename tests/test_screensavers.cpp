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
