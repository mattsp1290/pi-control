#include "screensaver/screensaver_view.h"

#include <QEvent>
#include <QPainter>

#include <fmt/base.h>

#include <algorithm>

namespace pi_control::screensaver {

namespace {

constexpr std::chrono::milliseconds kMaxStep{100};
constexpr std::size_t kMaxFrameSamples = 8192;
constexpr std::size_t kMinFramesForStatistics = 30;

float percentile(std::vector<float> samples, double fraction) {
    const auto index = std::min(samples.size() - 1, std::size_t(fraction * double(samples.size())));
    std::nth_element(samples.begin(), samples.begin() + std::ptrdiff_t(index), samples.end());
    return samples[index];
}

} // namespace

ScreensaverView::ScreensaverView(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    hide();
    frameTimer_.setTimerType(Qt::PreciseTimer);
    frameTimer_.setInterval(kFrameInterval);
    connect(&frameTimer_, &QTimer::timeout, this, &ScreensaverView::tick);
    frameIntervals_.reserve(kMaxFrameSamples);
    if (parent) {
        parent->installEventFilter(this);
        setGeometry(parent->rect());
    }
}

ScreensaverView::~ScreensaverView() {
    frameTimer_.stop();
}

void ScreensaverView::activate(QString id, std::unique_ptr<Screensaver> saver, quint32 seed) {
    deactivate();
    if (!saver) return;
    if (parentWidget()) setGeometry(parentWidget()->rect());
    id_ = std::move(id);
    saver_ = std::move(saver);
    saverSize_ = size();
    saver_->start(saverSize_, seed);
    frameIntervals_.clear();
    frameCount_ = 0;
    paintClock_.invalidate();
    raise();
    show();
    frameClock_.start();
    frameTimer_.start();
}

void ScreensaverView::deactivate() {
    frameTimer_.stop();
    if (!saver_) return;
    logFrameStatistics();
    hide();
    saver_.reset();
    id_.clear();
}

void ScreensaverView::tick() {
    if (!saver_) return;
    const auto elapsed = std::chrono::milliseconds(frameClock_.restart());
    saver_->advance(std::clamp(elapsed, std::chrono::milliseconds(0), kMaxStep));
    update();
}

bool ScreensaverView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
    }
    return QWidget::eventFilter(watched, event);
}

void ScreensaverView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // show() delivers the pending resize for the size start() already used.
    if (saver_ && size() != saverSize_) {
        saverSize_ = size();
        saver_->resize(saverSize_);
    }
}

void ScreensaverView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (!saver_) {
        painter.fillRect(rect(), Qt::black);
        return;
    }
    if (paintClock_.isValid()) {
        const float interval = float(paintClock_.nsecsElapsed()) / 1.0e6f;
        if (frameIntervals_.size() < kMaxFrameSamples) frameIntervals_.push_back(interval);
        else frameIntervals_[frameCount_ % kMaxFrameSamples] = interval;
        ++frameCount_;
    }
    paintClock_.start();
    saver_->paint(painter, rect());
}

void ScreensaverView::logFrameStatistics() {
    if (frameIntervals_.size() < kMinFramesForStatistics) return;
    fmt::print(stderr, "Screensaver {}: {} frames, median {:.1f} ms, p95 {:.1f} ms (last {} frames)\n",
               id_.toStdString(), frameCount_ + 1, percentile(frameIntervals_, 0.5),
               percentile(frameIntervals_, 0.95), frameIntervals_.size());
}

} // namespace pi_control::screensaver
