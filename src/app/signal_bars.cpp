#include "app/signal_bars.h"

#include <QPainter>

namespace pi_control::app {

SignalBars::SignalBars(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAccessibleName("Signal strength");
}

int SignalBars::barsFor(std::optional<int> strength) {
    if (!strength || *strength <= 0) return 0;
    if (*strength >= 75) return 4;
    if (*strength >= 50) return 3;
    if (*strength >= 25) return 2;
    return 1;
}

int SignalBars::litBars() const { return barsFor(strength_); }

void SignalBars::setStrength(std::optional<int> strength) {
    if (strength_ == strength) return;
    strength_ = strength;
    setAccessibleDescription(strength ? QString("%1 %").arg(*strength) : QString());
    update();
}

void SignalBars::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int lit = litBars();
    const int gap = 3;
    const int barWidth = (width() - 3 * gap) / 4;
    for (int i = 0; i < 4; ++i) {
        const int barHeight = height() * (i + 1) / 4;
        const QRectF bar(i * (barWidth + gap), height() - barHeight, barWidth, barHeight);
        painter.fillRect(bar, i < lit ? QColor("#ffffff") : QColor("#3b4d5d"));
    }
}

} // namespace pi_control::app
