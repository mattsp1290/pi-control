#include "app/screensaver_panel.h"

#include "app/settings_store.h"
#include "screensaver/registry.h"
#include "screensaver/screensaver_controller.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <fmt/base.h>

namespace pi_control::app {

ScreensaverPanel::ScreensaverPanel(screensaver::ScreensaverController& controller, SettingsStore& store,
                                   QWidget* parent)
    : QWidget(parent), controller_(controller), store_(store) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    const auto idleSeconds = std::chrono::duration_cast<std::chrono::seconds>(controller_.idleTimeout()).count();
    auto* hint = new QLabel(QString("Starts after %1 seconds without touch.").arg(idleSeconds));
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    const auto& screensavers = screensaver::builtinScreensavers();
    for (std::size_t i = 0; i < screensavers.size(); ++i) {
        auto* choice = new QRadioButton(screensavers[i].displayName);
        choice->setObjectName(screensavers[i].id);
        group_->addButton(choice, int(i));
        layout->addWidget(choice);
    }
    {
        const QSignalBlocker blocker(group_);
        const QString current = controller_.screensaverId();
        for (std::size_t i = 0; i < screensavers.size(); ++i) {
            if (screensavers[i].id == current) group_->button(int(i))->setChecked(true);
        }
    }
    connect(group_, &QButtonGroup::idClicked, this, &ScreensaverPanel::choose);

    status_ = new QLabel;
    status_->setObjectName("screensaverStatus");
    status_->setWordWrap(true);
    status_->hide();
    layout->addWidget(status_);

    preview_ = new QPushButton("Preview");
    preview_->setObjectName("previewButton");
    preview_->setMinimumWidth(180);
    connect(preview_, &QPushButton::clicked, &controller_, &screensaver::ScreensaverController::preview);
    auto* previewRow = new QHBoxLayout;
    previewRow->addStretch();
    previewRow->addWidget(preview_);
    layout->addLayout(previewRow);
}

void ScreensaverPanel::choose(int index) {
    const auto& screensavers = screensaver::builtinScreensavers();
    if (index < 0 || std::size_t(index) >= screensavers.size()) return;
    const auto& descriptor = screensavers[std::size_t(index)];
    // Re-choosing the current screensaver still saves it when the store
    // differs: a --screensaver override, or an earlier failed save.
    if (descriptor.id == controller_.screensaverId() && store_.screensaverId() == descriptor.id) return;
    controller_.setScreensaver(descriptor);
    if (store_.setScreensaverId(descriptor.id)) {
        fmt::print(stderr, "Screensaver: selected {}\n", descriptor.id.toStdString());
        status_->hide();
    } else {
        fmt::print(stderr, "Screensaver: could not save the choice to {}\n", store_.filePath().toStdString());
        status_->setText("Couldn't save. The choice lasts until restart.");
        status_->show();
    }
}

} // namespace pi_control::app
