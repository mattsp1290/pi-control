#include "app/elided_label.h"

#include <QPainter>
#include <QStyle>

namespace pi_control::app {

QSize ElidedLabel::minimumSizeHint() const {
    QSize size = QLabel::minimumSizeHint();
    size.setWidth(fontMetrics().horizontalAdvance(QStringLiteral("M…")) + contentsMargins().left()
                  + contentsMargins().right());
    return size;
}

bool ElidedLabel::isElided() const { return fontMetrics().horizontalAdvance(text()) > contentsRect().width(); }

void ElidedLabel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QRect area = contentsRect();
    const QString shown = fontMetrics().elidedText(text(), Qt::ElideRight, area.width());
    style()->drawItemText(&painter, area, int(alignment()), palette(), isEnabled(), shown, foregroundRole());
}

} // namespace pi_control::app
