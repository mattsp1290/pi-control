#pragma once

#include <QWidget>

namespace pi_control::app {

struct AppContext;

class HomePage final : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(const AppContext& context, QWidget* parent = nullptr);
};

} // namespace pi_control::app
