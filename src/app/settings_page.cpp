#include "app/settings_page.h"

#include "app/app_context.h"
#include "app/power_panel.h"
#include "app/screensaver_panel.h"
#include "app/update_panel.h"
#include "app/wifi_panel.h"
#include "screensaver/screensaver_controller.h"
#include "update/updater.h"

#include <QLabel>
#include <QScrollArea>
#include <QScroller>
#include <QVBoxLayout>

namespace pi_control::app {

SettingsPage::SettingsPage(const AppContext& context, QWidget* parent) : QWidget(parent) {
    auto* content = new QWidget;
    sections_ = new QVBoxLayout(content);
    sections_->setContentsMargins(24, 16, 24, 24);
    sections_->setSpacing(12);
    sections_->addStretch();

    scroll_ = new QScrollArea;
    scroll_->setWidget(content);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(scroll_->viewport(), QScroller::LeftMouseButtonGesture);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll_);

    // Fixed order; Power is always last.
    addSection("Wi‑Fi", new WifiPanel(context.wifi, context.shell, context.screensaver));
    addSection("Screensaver", new ScreensaverPanel(context.screensaver, context.settings));
    addSection("Software update", new UpdatePanel(context.updater, context.power.dryRun));
    power_ = new PowerPanel(context.power, context.screensaver);
    addSection("Power", power_);
    // While an update installs, its progress stays on screen and Shut down
    // waits: powering off would stop the updater partway through apt.
    const auto guardInstall = [this, &context](update::Updater::State state) {
        const bool installing = state == update::Updater::State::Installing;
        context.screensaver.setInhibited(&context.updater, installing);
        power_->setBlocked(installing ? "Shut down is unavailable while the software update installs." : QString());
    };
    connect(&context.updater, &update::Updater::stateChanged, this, guardInstall);
    guardInstall(context.updater.state());
}

void SettingsPage::addSection(const QString& title, QWidget* content) {
    auto* heading = new QLabel(title);
    heading->setObjectName("sectionTitle");
    // Insert before the trailing stretch.
    const int at = sections_->count() - 1;
    sections_->insertWidget(at, heading);
    sections_->insertWidget(at + 1, content);
    titles_ << title;
    lastSection_ = content;
}

} // namespace pi_control::app
