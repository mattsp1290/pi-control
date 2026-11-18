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

ScreensaverController::ScreensaverController(ScreensaverView& view, ScreensaverControllerConfig config,
                                             QObject* parent)
    : QObject(parent), view_(&view), config_(std::move(config)) {
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

    if (!enabled()) {
        fmt::print(stderr, "Screensaver: no screensaver; disabled\n");
        return;
    }
    Q_ASSERT(QCoreApplication::instance());
    QCoreApplication::instance()->installEventFilter(this);
    restartIdleTimer();
}

ScreensaverController::~ScreensaverController() {
    if (auto* app = QCoreApplication::instance()) app->removeEventFilter(this);
}

void ScreensaverController::setInhibited(const QObject* owner, bool inhibited) {
    const bool was = isInhibited();
    if (inhibited && !inhibitors_.contains(owner)) {
        inhibitors_.insert(owner);
        connect(owner, &QObject::destroyed, this, [this, owner] { setInhibited(owner, false); },
                Qt::SingleShotConnection);
    } else if (!inhibited && inhibitors_.remove(owner)) {
        disconnect(owner, &QObject::destroyed, this, nullptr);
    }
    if (isInhibited() == was) return;
    previewPending_ = false;
    if (!enabled()) return;
    if (isInhibited()) {
        idleTimer_.stop();
        if (state_ == State::Active) {
            if (view_) view_->deactivate();
            activeId_.clear();
            state_ = State::Watching;
        }
    } else if (watching()) {
        restartIdleTimer();
    }
}

void ScreensaverController::setScreensaver(const ScreensaverDescriptor& descriptor) {
    if (!enabled()) return; // A disabled controller never installed its event filter.
    config_.screensaver = &descriptor;
}

QString ScreensaverController::screensaverId() const {
    return config_.screensaver ? config_.screensaver->id : QString();
}

bool ScreensaverController::canShow() const { return view_ && !isInhibited() && watching() && enabled(); }

void ScreensaverController::preview() {
    if (!canShow()) return;
    if (anythingHeld()) {
        // Like an idle activation: covering a held press would swallow its
        // release, so show once everything is released.
        previewPending_ = true;
        return;
    }
    show(Trigger::Preview);
}

void ScreensaverController::showPendingPreviewIfReleased() {
    if (!previewPending_ || anythingHeld()) return;
    previewPending_ = false;
    // After the release has reached its widget.
    QTimer::singleShot(0, this, [this] {
        if (canShow()) show(Trigger::Preview);
    });
}

void ScreensaverController::restartIdleTimer() {
    sinceActivity_.start();
    if (!isInhibited()) idleTimer_.start();
}

bool ScreensaverController::anythingHeld() const {
    return heldButtons_ != Qt::NoButton || !heldTouchPoints_.isEmpty() || !heldKeys_.isEmpty();
}

void ScreensaverController::activate() {
    if (!canShow()) return;
    // A press resting on the screen sends no events. Covering it would swallow
    // its release and leave the widget under it pressed, so wait for release,
    // up to a cap in case the release was lost.
    if (anythingHeld() && heldSince_.isValid()
        && std::chrono::milliseconds(heldSince_.elapsed()) < config_.maxHoldDeferral) {
        idleTimer_.start();
        return;
    }
    show(Trigger::Idle);
}

void ScreensaverController::show(Trigger trigger) {
    const auto* descriptor = config_.screensaver;
    const quint32 seed = config_.random();
    idleTimer_.stop();
    previewPending_ = false;
    state_ = State::Active;
    activeId_ = descriptor->id;
    switch (trigger) {
    case Trigger::Idle:
        fmt::print(stderr, "Screensaver: {} after {} ms idle\n", activeId_.toStdString(), sinceActivity_.elapsed());
        break;
    case Trigger::Preview:
        fmt::print(stderr, "Screensaver: {} preview\n", activeId_.toStdString());
        break;
    }
    view_->activate(descriptor->id, descriptor->create(), seed);
    emit activated(activeId_);
}

void ScreensaverController::wake(QEvent* event) {
    if (view_) view_->deactivate();
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
    if (!anythingHeld() && elapsed >= config_.wakeGuard) {
        enterWatching(State::Watching);
        return;
    }
    if (elapsed >= config_.maxWakeGuard) {
        // Something is still held, or its release was lost. Stop blocking new
        // input, but drain the rest of this gesture (see eventFilter).
        enterWatching(anythingHeld() ? State::Draining : State::Watching);
        return;
    }
    // Re-check when the minimum guard ends, or force an exit at the maximum.
    const auto next = elapsed < config_.wakeGuard ? config_.wakeGuard : config_.maxWakeGuard;
    guardTimer_.start(std::max(std::chrono::milliseconds(1), next - elapsed));
}

void ScreensaverController::enterWatching(State next) {
    guardTimer_.stop();
    if (next == State::Watching) clearHeldInput();
    state_ = next;
    restartIdleTimer();
}

bool ScreensaverController::isNewPress(const QEvent* event) {
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::TouchBegin:
        return true;
    case QEvent::KeyPress:
        return !static_cast<const QKeyEvent*>(event)->isAutoRepeat();
    default:
        return false;
    }
}

bool ScreensaverController::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::ApplicationStateChange && state_ != State::WakeGuard
        && static_cast<QApplicationStateChangeEvent*>(event)->applicationState() != Qt::ApplicationActive) {
        // Releases may be lost while inactive. During the wake guard the held
        // state must survive, or the guard would end mid-gesture.
        clearHeldInput();
        if (state_ == State::Draining) state_ = State::Watching;
    }
    // Shortcuts are dispatched before the key press; never let one fire under
    // the screensaver or during its wake guard.
    if ((event->type() == QEvent::Shortcut || event->type() == QEvent::ShortcutOverride)
        && (state_ == State::Active || state_ == State::WakeGuard)) {
        return true;
    }
    const InputKind kind = classify(event->type());
    if (kind == InputKind::None) return QObject::eventFilter(watched, event);
    switch (state_) {
    case State::Draining:
        if (!isNewPress(event)) {
            // The rest of a gesture that outlived the wake guard.
            trackHeldInput(event);
            if (!anythingHeld()) state_ = State::Watching;
            return true;
        }
        state_ = State::Watching;
        clearHeldInput();
        [[fallthrough]];
    case State::Watching:
        if (event->spontaneous()) {
            const bool wasHeld = anythingHeld();
            trackHeldInput(event);
            if (!wasHeld && anythingHeld()) heldSince_.start();
            restartIdleTimer();
            showPendingPreviewIfReleased();
        }
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
