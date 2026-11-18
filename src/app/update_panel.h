#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace pi_control::screensaver {
class ScreensaverController;
}
namespace pi_control::update {
class Updater;
}

namespace pi_control::app {

// The running version, whether a newer release exists, and the button that
// installs it.
class UpdatePanel final : public QWidget {
    Q_OBJECT

public:
    UpdatePanel(update::Updater& updater, screensaver::ScreensaverController& screensaver, QWidget* parent = nullptr);

    QLabel* statusLabel() const { return status_; }
    QPushButton* checkButton() const { return check_; }
    QPushButton* updateButton() const { return update_; }

private:
    void refresh();

    update::Updater& updater_;
    screensaver::ScreensaverController& screensaver_;
    QLabel* status_;
    QPushButton* check_;
    QPushButton* update_;
    bool inhibiting_ = false;
};

} // namespace pi_control::app
