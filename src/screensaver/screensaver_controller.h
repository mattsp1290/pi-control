#pragma once

#include "screensaver/registry.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <chrono>
#include <functional>
#include <vector>

namespace pi_control::screensaver {

class ScreensaverView;

inline constexpr std::chrono::milliseconds kIdleTimeout{15000};
static_assert(kIdleTimeout == std::chrono::seconds(15));
inline constexpr std::chrono::milliseconds kWakeGuard{250};
inline constexpr std::chrono::milliseconds kMaxWakeGuard{3000};

// Uniform choice of a candidate index from one random draw.
inline std::size_t pickScreensaverIndex(const std::function<quint32()>& random, std::size_t count) {
    return count == 0 ? 0 : std::size_t(random() % count);
}

struct ScreensaverControllerConfig {
    std::chrono::milliseconds idleTimeout = kIdleTimeout;
    std::chrono::milliseconds wakeGuard = kWakeGuard;
    std::chrono::milliseconds maxWakeGuard = kMaxWakeGuard;
    std::vector<const ScreensaverDescriptor*> candidates = allBuiltins();
    std::function<quint32()> random; // Empty: QRandomGenerator::global().

    static std::vector<const ScreensaverDescriptor*> allBuiltins();
};

// Shows a random screensaver after idleTimeout without user input. The input
// that dismisses it, and the rest of that gesture, never reach other widgets.
class ScreensaverController final : public QObject {
    Q_OBJECT

public:
    enum class State { Watching, Active, WakeGuard };

    ScreensaverController(ScreensaverView& view, ScreensaverControllerConfig config, QObject* parent = nullptr);
    ~ScreensaverController() override;

    void setInhibited(bool inhibited);
    bool isInhibited() const { return inhibited_; }
    State state() const { return state_; }
    bool isActive() const { return state_ == State::Active; }
    QString activeScreensaverId() const { return activeId_; }

signals:
    void activated(const QString& id);
    void dismissed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void restartIdleTimer();
    void activate();
    void wake(QEvent* event);
    void trackHeldInput(QEvent* event);
    void clearHeldInput();
    void checkWakeGuard();
    void enterWatching();

    ScreensaverView& view_;
    ScreensaverControllerConfig config_;
    State state_ = State::Watching;
    bool inhibited_ = false;
    bool enabled_ = true;
    QString activeId_;
    QTimer idleTimer_;
    QTimer guardTimer_;
    QElapsedTimer sinceActivity_;
    QElapsedTimer guardClock_;
    Qt::MouseButtons heldButtons_;
    QSet<int> heldTouchPoints_;
    QSet<quint32> heldKeys_;
};

} // namespace pi_control::screensaver
