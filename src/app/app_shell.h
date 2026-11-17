#pragma once

#include <QStringView>
#include <QWidget>

#include <vector>

class QStackedWidget;
class QTabWidget;

namespace pi_control::screensaver {
class ScreensaverView;
}

namespace pi_control::app {

struct AppContext;

// The top-level window: a tabs layer, a task layer for full-window views such
// as password entry, and the screensaver overlay above both.
class AppShell final : public QWidget {
    Q_OBJECT

public:
    explicit AppShell(QWidget* parent = nullptr);

    // Creates the pages from tabs(). Call once, after every service in the
    // context exists.
    void buildTabs(const AppContext& context);

    screensaver::ScreensaverView& screensaverView() const { return *saverView_; }
    QTabWidget* tabWidget() const { return tabs_; }
    QWidget* page(QStringView id) const;

    // Covers the whole window, tab bar included, with view. The shell owns
    // view from the first call on; closeTask() only hides it.
    void showTask(QWidget* view);
    void closeTask();
    QWidget* currentTask() const;

private:
    QStackedWidget* layers_;
    QTabWidget* tabs_;
    screensaver::ScreensaverView* saverView_;
    std::vector<QString> pageIds_;
};

} // namespace pi_control::app
