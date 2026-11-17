#include "app/home_page.h"

#include <QLabel>
#include <QVBoxLayout>

namespace pi_control::app {

HomePage::HomePage(const AppContext&, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel("Home");
    title->setObjectName("pageTitle");
    layout->addWidget(title);
    layout->addStretch();
}

} // namespace pi_control::app
