#include "screensaver/screensaver_controller.h"

#include "screensaver/screensaver_view.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QTouchEvent>

#include <fmt/base.h>

namespace pi_control::screensaver {

namespace {

enum class InputKind { None, Activity, Wake };

InputKind classify(QEvent::Type type) {
    switch (type) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::KeyPress:
    case QEvent::Wheel:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
        return InputKind::Wake;
    case QEvent::MouseButtonRelease:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
    case QEvent::KeyRelease:
    case QEvent::TabletRelease:
        return InputKind::Activity;
    default:
        return InputKind::None;
    }
}

} // namespace

std::vector<const ScreensaverDescriptor*> ScreensaverControllerConfig::allBuiltins() {
    std::vector<const ScreensaverDescriptor*> all;
    for (const auto& descriptor : builtinScreensavers()) all.push_back(&descriptor);
    return all;
}

ScreensaverController::ScreensaverController(ScreensaverView& view, ScreensaverControllerConfig config,
                                             QObject* parent)
    : QObject(parent), view_(view), config_(std::move(config)) {
    if (!config_.random) {
        config_.random = [] { return QRandomGenerator::global()->generate(); };
    }
    idleTimer_.setSingleShot(true);
    idleTimer_.setTimerType(Qt::PreciseTimer);
    idleTimer_.setInterval(config_.idleTimeout);
    connect(&idleTimer_, &QTimer::timeout, this, &ScreensaverController::activate);
    guardTimer_.setSingleShot(true);
    guardTimer_.setTimerType(Qt::PreciseTimer);
    connect(&guardTimer_, &QTimer::timeout, this, &ScreensaverController::checkWakeGuard);

    enabled_ = !config_.candidates.empty();
    if (!enabled_) {
        fmt::print(stderr, "Screensaver: no candidates; disabled\n");
        return;
    }
    QCoreApplication::instance()->installEventFilter(this);
    restartIdleTimer();
}

ScreensaverController::~ScreensaverController() {
    if (auto* app = QCoreApplication::instance()) app->removeEventFilter(this);
}

void ScreensaverController::setInhibited(bool inhibited) {
    inhibited_ = inhibited;
    if (!enabled_) return;
    if (inhibited) {
        idleTimer_.stop();
        if (state_ == State::Active) {
            view_.deactivate();
            activeId_.clear();
            state_ = State::Watching;
        }
    } else if (state_ == State::Watching) {
        restartIdleTimer();
    }
}

void ScreensaverController::restartIdleTimer() {
    sinceActivity_.start();
    if (!inhibited_) idleTimer_.start();
}

void ScreensaverController::activate() {
    if (inhibited_ || state_ != State::Watching || config_.candidates.empty()) return;
    const auto* descriptor = config_.candidates[pickScreensaverIndex(config_.random, config_.candidates.size())];
    const quint32 seed = config_.random();
    state_ = State::Active;
    activeId_ = descriptor->id;
    fmt::print(stderr, "Screensaver: {} after {} ms idle\n", activeId_.toStdString(), sinceActivity_.elapsed());
    view_.activate(descriptor->id, descriptor->create(), seed);
    emit activated(activeId_);
}

void ScreensaverController::wake(QEvent* event) {
    view_.deactivate();
    activeId_.clear();
    state_ = State::WakeGuard;
    clearHeldInput();
    trackHeldInput(event);
    guardClock_.start();
    guardTimer_.start(config_.wakeGuard);
    emit dismissed();
}

void ScreensaverController::clearHeldInput() {
    heldButtons_ = Qt::NoButton;
    heldTouchPoints_.clear();
    heldKeys_.clear();
}

void ScreensaverController::trackHeldInput(QEvent* event) {
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove:
        heldButtons_ = static_cast<QMouseEvent*>(event)->buttons();
        break;
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate: {
        auto* touch = static_cast<QTouchEvent*>(event);
        // TouchBegin means no other point is down: drop ids from lost releases.
        if (event->type() == QEvent::TouchBegin) heldTouchPoints_.clear();
        for (const auto& point : touch->points()) {
            if (point.state() == QEventPoint::Released) heldTouchPoints_.remove(point.id());
            else heldTouchPoints_.insert(point.id());
        }
        break;
    }
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        heldTouchPoints_.clear();
        break;
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->isAutoRepeat()) break;
        const quint32 id = key->nativeScanCode() != 0 ? key->nativeScanCode() : quint32(key->key());
        if (event->type() == QEvent::KeyPress) heldKeys_.insert(id);
        else heldKeys_.remove(id);
        break;
    }
    default:
        break;
    }
}

void ScreensaverController::checkWakeGuard() {
    if (state_ != State::WakeGuard) return;
    const auto elapsed = std::chrono::milliseconds(guardClock_.elapsed());
    const bool released = heldButtons_ == Qt::NoButton && heldTouchPoints_.isEmpty() && heldKeys_.isEmpty();
    if (elapsed >= config_.maxWakeGuard || (released && elapsed >= config_.wakeGuard)) {
        enterWatching();
        return;
    }
    // Re-check when the minimum guard ends, or force an exit at the maximum.
    const auto next = elapsed < config_.wakeGuard ? config_.wakeGuard : config_.maxWakeGuard;
    guardTimer_.start(std::max(std::chrono::milliseconds(1), next - elapsed));
}

void ScreensaverController::enterWatching() {
    guardTimer_.stop();
    clearHeldInput();
    state_ = State::Watching;
    restartIdleTimer();
}

bool ScreensaverController::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ApplicationStateChange
        && static_cast<QApplicationStateChangeEvent*>(event)->applicationState() != Qt::ApplicationActive) {
        clearHeldInput();
    }
    const InputKind kind = classify(event->type());
    if (kind == InputKind::None) return QObject::eventFilter(watched, event);
    switch (state_) {
    case State::Watching:
        if (event->spontaneous()) restartIdleTimer();
        return false;
    case State::Active:
        if (kind == InputKind::Wake && event->spontaneous()) wake(event);
        return true;
    case State::WakeGuard:
        trackHeldInput(event);
        checkWakeGuard();
        return true;
    }
    return false;
}

} // namespace pi_control::screensaver
