#pragma once

#include "screensaver/screensaver.h"

#include <QImage>
#include <QPainter>

namespace pi_control::screensaver {

// A Screensaver that renders a reduced-resolution frame and scales it to the
// view. Subclasses size the frame, mark it stale when the simulation moves,
// and draw it in render().
class RasterScreensaver : public Screensaver {
public:
    void paint(QPainter& painter, const QRect& target) final {
        if (target.isEmpty()) return;
        if (frame_.isNull()) {
            painter.fillRect(target, QColor::fromRgb(emptyColor_));
            return;
        }
        if (stale_) {
            render(frame_);
            stale_ = false;
        }
        painter.setRenderHint(QPainter::SmoothPixmapTransform, smoothScaling_);
        painter.drawImage(target, frame_);
        paintOverlay(painter, target);
    }

protected:
    RasterScreensaver(QRgb emptyColor, bool smoothScaling) : emptyColor_(emptyColor), smoothScaling_(smoothScaling) {}

    // An empty size gives no frame; paint() then fills with the empty colour.
    void setFrameSize(QSize size) {
        frame_ = size.isEmpty() ? QImage() : QImage(size, QImage::Format_RGB32);
        stale_ = true;
    }
    void invalidate() { stale_ = true; }

    // Draw every pixel of frame from the current simulation state.
    virtual void render(QImage& frame) = 0;
    // Drawn over the scaled frame, for effects such as fades.
    virtual void paintOverlay(QPainter&, const QRect&) {}

private:
    QImage frame_;
    bool stale_ = true;
    QRgb emptyColor_;
    bool smoothScaling_;
};

} // namespace pi_control::screensaver
