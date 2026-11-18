#include "app/settings_page.h"

#include "app/app_context.h"
#include "app/power_panel.h"
#include "app/screensaver_panel.h"
#include "app/update_panel.h"
#include "app/wifi_panel.h"
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
    addSection("Software update", new UpdatePanel(context.updater, context.screensaver));
    power_ = new PowerPanel(context.power, context.screensaver);
    addSection("Power", power_);
    // Powering off would stop the updater partway through apt.
    const auto blockDuringUpdate = [this](update::Updater::State state) {
        power_->setBlocked(state == update::Updater::State::Installing
                               ? "Shut down is unavailable while the software update installs."
                               : QString());
    };
    connect(&context.updater, &update::Updater::stateChanged, power_, blockDuringUpdate);
    blockDuringUpdate(context.updater.state());
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
