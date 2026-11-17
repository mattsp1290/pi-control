#pragma once

#include <QWidget>

#include <optional>

namespace pi_control::app {

// Four bars lit at >= 75, >= 50, >= 25 and > 0 percent.
class SignalBars final : public QWidget {
    Q_OBJECT

public:
    explicit SignalBars(QWidget* parent = nullptr);

    void setStrength(std::optional<int> strength);
    int litBars() const;
    QSize sizeHint() const override { return {36, 28}; }

    static int barsFor(std::optional<int> strength);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::optional<int> strength_;
};

} // namespace pi_control::app
