#include "app/app_shell.h"

#include "app/tabs.h"
#include "screensaver/screensaver_view.h"

#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace pi_control::app {

namespace {

constexpr auto kStyleSheet = R"(
QWidget { background: #101820; color: #ffffff; font-size: 22px; }
QLabel { background: transparent; }
QLabel#pageTitle { font-size: 30px; font-weight: bold; }
QLabel#sectionTitle { font-size: 26px; font-weight: bold; padding: 16px 0px 0px 0px; }
QLabel#hint, QLabel#powerStatus { color: #b8c4cf; }
QTabWidget::pane { border: none; }
QTabBar::tab { min-height: 64px; font-size: 24px; color: #9fb0bf; background: #17222c;
               border: none; border-bottom: 4px solid transparent; padding: 0 16px; }
QTabBar::tab:selected { color: #ffffff; border-bottom-color: #4fa3e0; }
QPushButton, QToolButton { background: #24313d; border: 2px solid #3b4d5d; border-radius: 10px;
                           min-height: 56px; font-size: 22px; padding: 0 16px; }
QPushButton:pressed, QToolButton:pressed { background: #3b4d5d; }
QPushButton:disabled, QToolButton:disabled { color: #6b7782; border-color: #2a3540; }
QPushButton:focus { border-color: #ffffff; }
QPushButton#shutdownButton { background: #b52d3a; border: 3px solid #ef8e98; border-radius: 18px;
                             padding: 24px; font-size: 32px; }
QPushButton#shutdownButton:focus { border-color: white; }
QPushButton#shutdownButton:pressed { background: #86202b; }
QPushButton#shutdownButton:disabled { background: #555; border-color: #777; }
QProgressBar { background: #24313d; border: none; border-radius: 6px; max-height: 18px; }
QProgressBar::chunk { background: #4fa3e0; border-radius: 6px; }
QScrollBar:vertical { width: 8px; background: transparent; }
QScrollBar::handle:vertical { background: #3b4d5d; border-radius: 4px; min-height: 40px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QRadioButton { min-height: 56px; spacing: 16px; }
QRadioButton::indicator { width: 26px; height: 26px; border: 3px solid #9fb0bf; border-radius: 16px;
                          background: transparent; }
QRadioButton::indicator:checked { border-color: #4fa3e0;
    background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5,
                                stop:0 #4fa3e0, stop:0.55 #4fa3e0, stop:0.6 transparent); }
QLineEdit { background: #f4f7fa; color: #101820; border: 2px solid #3b4d5d; border-radius: 10px;
            min-height: 52px; padding: 0 12px; font-size: 26px; }
QLineEdit:focus { border-color: #4fa3e0; }
QLineEdit:disabled { background: #9aa6b1; }
)";

} // namespace

AppShell::AppShell(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Pi Control");
    setMinimumSize(320, 240);
    setStyleSheet(kStyleSheet);
    tabs_ = new QTabWidget;
    tabs_->setTabPosition(QTabWidget::North);
    tabs_->setDocumentMode(true);
    tabs_->tabBar()->setExpanding(true);
    tabs_->tabBar()->setDrawBase(false);
    layers_ = new QStackedWidget;
    layers_->addWidget(tabs_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(layers_);
    // ScreensaverView raises itself on activation, so it stays above every layer.
    saverView_ = new screensaver::ScreensaverView(this);
}

void AppShell::buildTabs(const AppContext& context) {
    Q_ASSERT(pageIds_.empty());
    for (const auto& tab : tabs()) {
        QWidget* page = tab.create(context);
        page->setObjectName(tab.id + "Page");
        tabs_->addTab(page, tab.title);
        pageIds_.push_back(tab.id);
    }
    tabs_->setCurrentIndex(0);
}

QWidget* AppShell::page(QStringView id) const {
    for (std::size_t i = 0; i < pageIds_.size(); ++i) {
        if (pageIds_[i] == id) return tabs_->widget(int(i));
    }
    return nullptr;
}

void AppShell::showTask(QWidget* view) {
    if (layers_->indexOf(view) < 0) layers_->addWidget(view);
    layers_->setCurrentWidget(view);
}

void AppShell::closeTask() { layers_->setCurrentWidget(tabs_); }

QWidget* AppShell::currentTask() const {
    return layers_->currentWidget() == tabs_ ? nullptr : layers_->currentWidget();
}

} // namespace pi_control::app
