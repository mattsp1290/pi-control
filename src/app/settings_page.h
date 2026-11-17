#pragma once

#include <QStringList>
#include <QWidget>

class QScrollArea;
class QVBoxLayout;

namespace pi_control::app {

struct AppContext;
class PowerPanel;

// Settings sections in one vertical, touch-scrollable page. The page's scroll
// area is the only scroller: no section scrolls on its own.
class SettingsPage final : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(const AppContext& context, QWidget* parent = nullptr);

    void addSection(const QString& title, QWidget* content);
    QStringList sectionTitles() const { return titles_; }
    QWidget* lastSection() const { return lastSection_; }
    QScrollArea* scrollArea() const { return scroll_; }
    PowerPanel* powerPanel() const { return power_; }

private:
    QScrollArea* scroll_;
    QVBoxLayout* sections_;
    QStringList titles_;
    QWidget* lastSection_ = nullptr;
    PowerPanel* power_;
};

} // namespace pi_control::app
