#pragma once

#include "screensaver/screensaver.h"

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <vector>

namespace pi_control::screensaver {

inline constexpr std::chrono::milliseconds kFrameInterval{33};

// Full-parent overlay that drives one active Screensaver. It never handles
// input; ScreensaverController decides when it is shown and hidden.
class ScreensaverView final : public QWidget {
    Q_OBJECT

public:
    explicit ScreensaverView(QWidget* parent);
    ~ScreensaverView() override;

    void activate(QString id, std::unique_ptr<Screensaver> saver, quint32 seed);
    void deactivate();
    bool isActive() const { return saver_ != nullptr; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void tick();
    void logFrameStatistics();

    std::unique_ptr<Screensaver> saver_;
    QString id_;
    QTimer frameTimer_;
    QElapsedTimer frameClock_;
    QElapsedTimer paintClock_;
    std::vector<float> frameIntervals_;
};

} // namespace pi_control::screensaver
