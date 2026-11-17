#pragma once

#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

namespace pi_control::app {

struct AppContext;

struct TabDescriptor {
    QString id;    // Stable lowercase id used by tests and smoke runs.
    QString title; // Text on the tab bar.
    std::function<QWidget*(const AppContext&)> create;
};

// Every tab, in tab-bar order. See docs/tabs.md to add one.
const std::vector<TabDescriptor>& tabs();

} // namespace pi_control::app
