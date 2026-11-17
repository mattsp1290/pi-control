#pragma once

#include <QLabel>

namespace pi_control::app {

// A single-line label that may shrink below its text width and then ends the
// text with "…". text() always returns the full text.
class ElidedLabel final : public QLabel {
    Q_OBJECT

public:
    using QLabel::QLabel;

    QSize minimumSizeHint() const override;
    bool isElided() const;

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace pi_control::app
