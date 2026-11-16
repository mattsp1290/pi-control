#include "screensaver/screensaver_controller.h"
#include "screensaver/screensaver_view.h"

#include <QApplication>
#include <QPainter>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

#include <memory>
#include <random>

using namespace pi_control::screensaver;
using namespace std::chrono_literals;
using State = ScreensaverController::State;

namespace {

struct SaverCounters {
    int advances = 0;
    int paints = 0;
};
SaverCounters counters;

class CountingSaver final : public Screensaver {
public:
    void start(QSize, quint32) override {}
    void resize(QSize) override {}
    void advance(std::chrono::milliseconds) override { ++counters.advances; }
    void paint(QPainter& painter, const QRect& target) override {
        ++counters.paints;
        painter.fillRect(target, Qt::black);
    }
};

const std::vector<ScreensaverDescriptor>& testDescriptors() {
    static const std::vector<ScreensaverDescriptor> descriptors{
        {"alpha", "Alpha", [] { return std::make_unique<CountingSaver>(); }},
        {"beta", "Beta", [] { return std::make_unique<CountingSaver>(); }},
        {"gamma", "Gamma", [] { return std::make_unique<CountingSaver>(); }},
    };
    return descriptors;
}

std::vector<const ScreensaverDescriptor*> allTestCandidates() {
    std::vector<const ScreensaverDescriptor*> all;
    for (const auto& descriptor : testDescriptors()) all.push_back(&descriptor);
    return all;
}

// Counts the input events that actually reach a widget.
class InputCounter final : public QObject {
public:
    int keyPresses = 0;
    int mousePresses = 0;
    int touchBegins = 0;

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        switch (event->type()) {
        case QEvent::KeyPress: ++keyPresses; break;
        case QEvent::MouseButtonPress: ++mousePresses; break;
        case QEvent::TouchBegin: ++touchBegins; break;
        default: break;
        }
        return false;
    }
};

QPointingDevice* touchDevice() {
    static QPointingDevice* device = QTest::createTouchDevice();
    return device;
}

} // namespace

class ScreensaverControllerTests : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QWidget> parent_;
    QPushButton* button_ = nullptr;
    ScreensaverView* view_ = nullptr;
    std::unique_ptr<ScreensaverController> controller_;
    std::unique_ptr<InputCounter> counter_;
    std::unique_ptr<QSignalSpy> clicked_;
    std::vector<quint32> script_;
    std::size_t scriptPosition_ = 0;

    ScreensaverControllerConfig defaultConfig() {
        ScreensaverControllerConfig config;
        config.idleTimeout = 300ms;
        config.wakeGuard = 100ms;
        config.maxWakeGuard = 3000ms;
        config.candidates = allTestCandidates();
        return config;
    }

    void build(ScreensaverControllerConfig config) {
        parent_ = std::make_unique<QWidget>();
        parent_->resize(320, 240);
        auto* layout = new QVBoxLayout(parent_.get());
        button_ = new QPushButton("Shut down");
        button_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        layout->addWidget(button_);
        counter_ = std::make_unique<InputCounter>();
        button_->installEventFilter(counter_.get());
        clicked_ = std::make_unique<QSignalSpy>(button_, &QPushButton::clicked);
        view_ = new ScreensaverView(parent_.get());
        controller_ = std::make_unique<ScreensaverController>(*view_, std::move(config));
        parent_->show();
        QVERIFY(QTest::qWaitForWindowActive(parent_.get()));
        button_->setFocus();
        QCOMPARE(QApplication::focusWidget(), button_);
    }

    QWindow* window() const { return parent_->windowHandle(); }
    QPoint buttonCenter() const { return button_->mapTo(parent_.get(), button_->rect().center()); }

    void waitUntilActive() {
        QTRY_VERIFY_WITH_TIMEOUT(controller_->isActive(), 3000);
    }
    void waitUntilWatching() {
        QTRY_VERIFY_WITH_TIMEOUT(controller_->state() == State::Watching, 5000);
    }
    void click() {
        QTest::mouseClick(window(), Qt::LeftButton, {}, buttonCenter());
    }
    void touchPress() { QTest::touchEvent(window(), touchDevice()).press(0, buttonCenter()); }
    void touchRelease() { QTest::touchEvent(window(), touchDevice()).release(0, buttonCenter()); }

private slots:
    void init() {
        counters = {};
        script_.clear();
        scriptPosition_ = 0;
    }

    void cleanup() {
        controller_.reset();
        clicked_.reset();
        parent_.reset();
        counter_.reset();
    }

    void pressHeldOffButtonDefersActivation() {
        build(defaultConfig());
        const QPoint margin(2, 2); // Inside the window, outside the button.
        QVERIFY(!button_->geometry().contains(margin));
        QTest::touchEvent(window(), touchDevice()).press(0, margin);
        QTest::qWait(1000);
        QCOMPARE(controller_->state(), State::Watching);
        QTest::touchEvent(window(), touchDevice()).release(0, margin);
        waitUntilActive();
        QCOMPARE(clicked_->count(), 0);
    }

    void longTouchWakeAfterCappedDeferralNeverClicks() {
        auto config = defaultConfig();
        config.maxWakeGuard = 500ms;
        config.maxHoldDeferral = 600ms;
        build(std::move(config));
        touchPress(); // Rests on the button until the deferral cap.
        waitUntilActive();
        touchRelease(); // Swallowed: the button stays down.
        touchPress();   // Wake, held past maxWakeGuard.
        QTest::qWait(800);
        QCOMPARE(controller_->state(), State::Watching);
        touchRelease();
        QCOMPARE(clicked_->count(), 0);
        touchPress();
        touchRelease();
        QCOMPARE(clicked_->count(), 1);
    }

    void longMouseWakeAfterCappedDeferralNeverClicks() {
        auto config = defaultConfig();
        config.maxWakeGuard = 500ms;
        config.maxHoldDeferral = 600ms;
        build(std::move(config));
        QTest::mousePress(window(), Qt::LeftButton, {}, buttonCenter());
        waitUntilActive();
        QTest::mouseRelease(window(), Qt::LeftButton, {}, buttonCenter());
        QTest::mousePress(window(), Qt::LeftButton, {}, buttonCenter());
        QTest::qWait(800);
        QCOMPARE(controller_->state(), State::Watching);
        QTest::mouseRelease(window(), Qt::LeftButton, {}, buttonCenter());
        QCOMPARE(clicked_->count(), 0);
        click();
        QCOMPARE(clicked_->count(), 1);
    }

    void shortcutWhileActiveDoesNotFire() {
        build(defaultConfig());
        button_->setShortcut(Qt::Key_S);
        // The QWidget overload goes through the shortcut map, like a real key.
        QTest::keyClick(button_, Qt::Key_S); // Positive control while watching.
        QTRY_COMPARE(clicked_->count(), 1);  // Shortcuts use animateClick().
        waitUntilActive();
        QTest::keyClick(button_, Qt::Key_S);
        QTest::qWait(400);
        QCOMPARE(clicked_->count(), 1);
    }

    void defaultConfigUsesFifteenSeconds() {
        const ScreensaverControllerConfig config;
        QCOMPARE(config.idleTimeout, kIdleTimeout);
        QCOMPARE(config.candidates.size(), builtinScreensavers().size());
    }

    void activatesAfterIdleTimeout() {
        QElapsedTimer elapsed;
        elapsed.start();
        build(defaultConfig());
        waitUntilActive();
        QVERIFY(elapsed.elapsed() >= 300);
        QVERIFY(view_->isVisible());
        QCOMPARE(view_->geometry(), parent_->rect());
        QTRY_VERIFY(counters.paints > 0);
    }

    void showingViewDoesNotDismissIt() {
        build(defaultConfig());
        waitUntilActive();
        QTest::qWait(500);
        QVERIFY(controller_->isActive());
    }

    void activityRestartsIdleTimer() {
        build(defaultConfig());
        for (int i = 0; i < 20; ++i) {
            QTest::mouseMove(window(), QPoint(10 + i % 2, 10));
            QTest::qWait(50);
            QVERIFY(!controller_->isActive());
        }
        waitUntilActive();
    }

    void clickWhileActiveDoesNotReachButton() {
        build(defaultConfig());
        QSignalSpy dismissed(controller_.get(), &ScreensaverController::dismissed);
        waitUntilActive();
        click();
        QCOMPARE(clicked_->count(), 0);
        QCOMPARE(counter_->mousePresses, 0);
        QVERIFY(!controller_->isActive());
        QCOMPARE(dismissed.count(), 1);
    }

    void touchWhileActiveDoesNotReachButton() {
        build(defaultConfig());
        waitUntilActive();
        touchPress();
        QTest::qWait(50);
        touchRelease();
        QCOMPARE(clicked_->count(), 0);
        QCOMPARE(counter_->touchBegins, 0);
        QCOMPARE(counter_->mousePresses, 0);
        QVERIFY(!controller_->isActive());
    }

    void heldTouchKeepsGuard() {
        build(defaultConfig());
        waitUntilActive();
        touchPress();
        QTest::qWait(400);
        QCOMPARE(controller_->state(), State::WakeGuard);
        touchRelease();
        QCOMPARE(clicked_->count(), 0);
        waitUntilWatching();
    }

    void heldMouseKeepsGuard() {
        build(defaultConfig());
        waitUntilActive();
        QTest::mousePress(window(), Qt::LeftButton, {}, buttonCenter());
        QTest::qWait(400);
        QCOMPARE(controller_->state(), State::WakeGuard);
        QTest::mouseRelease(window(), Qt::LeftButton, {}, buttonCenter());
        QCOMPARE(clicked_->count(), 0);
        waitUntilWatching();
    }

    void touchWakeThenTouchReachesButton() {
        build(defaultConfig());
        waitUntilActive();
        touchPress();
        touchRelease();
        waitUntilWatching();
        touchPress();
        touchRelease();
        QCOMPARE(clicked_->count(), 1);
    }

    void inputAfterGuardReachesButton() {
        build(defaultConfig());
        waitUntilActive();
        click();
        waitUntilWatching();
        click();
        QCOMPARE(clicked_->count(), 1);
    }

    void lostTouchEndDoesNotBlockInput() {
        auto config = defaultConfig();
        config.maxWakeGuard = 500ms;
        build(std::move(config));
        waitUntilActive();
        touchPress();
        waitUntilWatching();
        touchPress();
        touchRelease();
        QCOMPARE(clicked_->count(), 1);
    }

    void mismatchedKeyReleaseDoesNotBlockInput() {
        auto config = defaultConfig();
        config.maxWakeGuard = 500ms;
        build(std::move(config));
        waitUntilActive();
        QTest::keyPress(window(), Qt::Key_Space);
        QTest::keyRelease(window(), Qt::Key_A);
        QCOMPARE(controller_->state(), State::WakeGuard);
        waitUntilWatching();
        click();
        QCOMPARE(clicked_->count(), 1);
    }

    void spaceWhenIdleReachesButton() {
        build(defaultConfig());
        QTest::keyClick(window(), Qt::Key_Space);
        QCOMPARE(counter_->keyPresses, 1);
        QCOMPARE(clicked_->count(), 1);
    }

    void spaceWhileActiveDoesNotReachButton() {
        build(defaultConfig());
        waitUntilActive();
        QTest::keyClick(window(), Qt::Key_Space);
        QCOMPARE(counter_->keyPresses, 0);
        QCOMPARE(clicked_->count(), 0);
    }

    void keyAfterGuardReachesButton() {
        build(defaultConfig());
        waitUntilActive();
        QTest::keyClick(window(), Qt::Key_Space);
        waitUntilWatching();
        QTest::keyClick(window(), Qt::Key_Space);
        QCOMPARE(clicked_->count(), 1);
    }

    void returnWhileActiveDoesNotReachButton() {
        build(defaultConfig());
        waitUntilActive();
        QTest::keyClick(window(), Qt::Key_Return);
        QCOMPARE(counter_->keyPresses, 0);
    }

    void inhibitedNeverActivates() {
        build(defaultConfig());
        controller_->setInhibited(true);
        QTest::qWait(1500);
        QVERIFY(!controller_->isActive());
    }

    void inhibitDismissesActiveSaver() {
        build(defaultConfig());
        waitUntilActive();
        controller_->setInhibited(true);
        QVERIFY(!controller_->isActive());
        QVERIFY(!view_->isVisible());
        QCOMPARE(controller_->state(), State::Watching);
    }

    void uninhibitRestartsTimer() {
        build(defaultConfig());
        controller_->setInhibited(true);
        QTest::qWait(500);
        controller_->setInhibited(false);
        QVERIFY(!controller_->isActive());
        waitUntilActive();
    }

    void inhibitDuringGuardKeepsGuard() {
        build(defaultConfig());
        waitUntilActive();
        QTest::mousePress(window(), Qt::LeftButton, {}, buttonCenter());
        controller_->setInhibited(true);
        QCOMPARE(controller_->state(), State::WakeGuard);
        QTest::mouseRelease(window(), Qt::LeftButton, {}, buttonCenter());
        QTest::qWait(1000);
        QCOMPARE(clicked_->count(), 0);
        QCOMPARE(controller_->state(), State::Watching);
        QVERIFY(!controller_->isActive());
    }

    void emptyCandidatesNeverActivate() {
        auto config = defaultConfig();
        config.candidates.clear();
        build(std::move(config));
        QTest::qWait(1500);
        QVERIFY(!controller_->isActive());
        click();
        QCOMPARE(clicked_->count(), 1);
    }

    void selectionUsesInjectedRandom() {
        auto config = defaultConfig();
        script_ = {2, 11, 0, 12};
        config.random = [this] { return script_[scriptPosition_++ % script_.size()]; };
        build(std::move(config));
        QSignalSpy activated(controller_.get(), &ScreensaverController::activated);
        waitUntilActive();
        QCOMPARE(controller_->activeScreensaverId(), QString("gamma"));
        click();
        waitUntilWatching();
        waitUntilActive();
        QCOMPARE(controller_->activeScreensaverId(), QString("alpha"));
        QCOMPARE(activated.count(), 2);
    }

    void selectionIsUniform() {
        std::mt19937 generator(12345);
        const std::function<quint32()> random = [&] { return quint32(generator()); };
        int counts[3] = {};
        for (int i = 0; i < 300; ++i) ++counts[pickScreensaverIndex(random, 3)];
        for (const int count : counts) QVERIFY2(count >= 60, qPrintable(QString::number(count)));
    }

    void pinnedCandidateAlwaysChosen() {
        auto config = defaultConfig();
        config.candidates = {&testDescriptors()[1]};
        build(std::move(config));
        for (int i = 0; i < 5; ++i) {
            waitUntilActive();
            QCOMPARE(controller_->activeScreensaverId(), QString("beta"));
            click();
            waitUntilWatching();
        }
    }

    void noPaintAfterDeactivate() {
        build(defaultConfig());
        waitUntilActive();
        QTRY_VERIFY(counters.paints > 0 && counters.advances > 0);
        view_->deactivate();
        const SaverCounters snapshot = counters;
        QTest::qWait(300);
        QCOMPARE(counters.advances, snapshot.advances);
        QCOMPARE(counters.paints, snapshot.paints);
    }
};

QTEST_MAIN(ScreensaverControllerTests)
#include "test_screensaver_controller.moc"
