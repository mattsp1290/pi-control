#pragma once

#include <QWidget>

namespace pi_control::app {

// The example of adding a tab: static text in a touch-scrollable page.
class ExamplePage final : public QWidget {
    Q_OBJECT

public:
    explicit ExamplePage(QWidget* parent = nullptr);
};

} // namespace pi_control::app
