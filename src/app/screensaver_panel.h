#pragma once

#include <QWidget>

class QButtonGroup;
class QLabel;
class QPushButton;

namespace pi_control::screensaver {
class ScreensaverController;
}

namespace pi_control::app {

class SettingsStore;

// Chooses which screensaver starts after the idle timeout, and previews it.
class ScreensaverPanel final : public QWidget {
    Q_OBJECT

public:
    ScreensaverPanel(screensaver::ScreensaverController& controller, SettingsStore& store, QWidget* parent = nullptr);

    QButtonGroup* choices() const { return group_; }
    QPushButton* previewButton() const { return preview_; }
    QLabel* statusLabel() const { return status_; }

private:
    void choose(int index);

    screensaver::ScreensaverController& controller_;
    SettingsStore& store_;
    QButtonGroup* group_;
    QPushButton* preview_;
    QLabel* status_;
};

} // namespace pi_control::app
