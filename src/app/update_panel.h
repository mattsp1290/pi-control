#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace pi_control::update {
class Updater;
}

namespace pi_control::app {

// The running version, whether a newer release exists, and the button that
// installs it.
class UpdatePanel final : public QWidget {
    Q_OBJECT

public:
    // dryRun only changes the wording while an install is simulated.
    UpdatePanel(update::Updater& updater, bool dryRun, QWidget* parent = nullptr);

    QLabel* statusLabel() const { return status_; }
    QPushButton* checkButton() const { return check_; }
    QPushButton* updateButton() const { return update_; }

private:
    void refresh();

    update::Updater& updater_;
    bool dryRun_;
    QLabel* status_;
    QPushButton* check_;
    QPushButton* update_;
};

} // namespace pi_control::app
