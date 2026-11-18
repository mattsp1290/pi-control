#include "app/update_panel.h"

#include "update/updater.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace pi_control::app {

using update::Updater;

UpdatePanel::UpdatePanel(Updater& updater, bool dryRun, QWidget* parent)
    : QWidget(parent), updater_(updater), dryRun_(dryRun) {
    status_ = new QLabel;
    status_->setObjectName("updateStatus");
    status_->setWordWrap(true);
    check_ = new QPushButton("Check for updates");
    check_->setObjectName("checkUpdatesButton");
    update_ = new QPushButton;
    update_->setObjectName("updateButton");
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(check_);
    buttons->addWidget(update_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(status_);
    layout->addLayout(buttons);
    connect(check_, &QPushButton::clicked, &updater_, &Updater::check);
    connect(update_, &QPushButton::clicked, &updater_, &Updater::install);
    connect(&updater_, &Updater::stateChanged, this, &UpdatePanel::refresh);
    refresh();
}

void UpdatePanel::refresh() {
    const QString current = updater_.current().toString();
    const QString offer = updater_.offer() ? updater_.offer()->toString() : QString();
    const Updater::State state = updater_.state();
    switch (state) {
    case Updater::State::Idle:
        status_->setText(QString("Version %1.").arg(current));
        break;
    case Updater::State::Checking:
        status_->setText(QString("Version %1. Checking for updates…").arg(current));
        break;
    case Updater::State::UpToDate:
        status_->setText(QString("Version %1 is up to date.").arg(current));
        break;
    case Updater::State::Available:
        status_->setText(QString("Version %1 is available. This is version %2.").arg(offer, current));
        break;
    case Updater::State::CheckFailed:
        status_->setText(QString("Version %1. Couldn't check for updates. Check the network and try again.")
                             .arg(current));
        break;
    case Updater::State::Installing:
        status_->setText(dryRun_ ? QString("Dry run: update to %1 requested.").arg(offer)
                                 : QString("Updating to the latest release (%1 or newer)… The screen restarts "
                                           "when it's done.")
                                       .arg(offer));
        break;
    case Updater::State::InstallFailed:
        status_->setText("Update failed. Check journalctl -u pi-control-update and try again.");
        break;
    case Updater::State::NothingInstalled:
        status_->setText("The installed package is already the latest release. Restart Pi Control to run it.");
        break;
    }
    check_->setEnabled(!updater_.busy());
    update_->setVisible(updater_.offer().has_value());
    update_->setEnabled(updater_.canInstall());
    update_->setText(QString("Update to %1").arg(offer));
}

} // namespace pi_control::app
