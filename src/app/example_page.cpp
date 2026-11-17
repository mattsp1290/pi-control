#include "app/example_page.h"

#include <QLabel>
#include <QScrollArea>
#include <QScroller>
#include <QVBoxLayout>

namespace pi_control::app {

namespace {

constexpr auto kLoremIpsum =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore "
    "et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut "
    "aliquip ex ea commodo consequat.\n\n"
    "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
    "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit "
    "anim id est laborum.\n\n"
    "Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, "
    "totam rem aperiam, eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae "
    "dicta sunt explicabo.";

} // namespace

ExamplePage::ExamplePage(QWidget* parent) : QWidget(parent) {
    auto* title = new QLabel("Example");
    title->setObjectName("pageTitle");
    auto* body = new QLabel(kLoremIpsum);
    body->setObjectName("exampleBody");
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 16, 24, 16);
    contentLayout->setSpacing(16);
    contentLayout->addWidget(title);
    contentLayout->addWidget(body);
    contentLayout->addStretch();

    auto* scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
}

} // namespace pi_control::app
