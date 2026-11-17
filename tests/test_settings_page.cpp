#include "app_fixture.h"

#include "app/power_panel.h"
#include "app/settings_page.h"

#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

using namespace pi_control::app;

class SettingsPageTests : public QObject {
    Q_OBJECT

private slots:
    void sectionsInOrder() {
        AppFixture fixture;
        auto* settings = fixture.page<SettingsPage>(u"settings");
        QCOMPARE(settings->sectionTitles(), (QStringList{"Power"}));
        QCOMPARE(settings->lastSection(), settings->powerPanel());
    }

    void tapWhileScrolledClicksOnce() {
        AppFixture fixture;
        auto* settings = fixture.page<SettingsPage>(u"settings");
        auto* spacer = new QWidget;
        spacer->setFixedHeight(900);
        settings->addSection("Spacer", spacer);
        // Keep Power last, as the page does in production.
        auto* power = settings->powerPanel();
        settings->addSection("Power", power);
        fixture.showTab(u"settings");
        fixture.shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(fixture.shell.get()));
        QScrollBar* bar = settings->scrollArea()->verticalScrollBar();
        QVERIFY(bar->maximum() > 0);
        bar->setValue(bar->maximum() / 2);
        settings->scrollArea()->ensureWidgetVisible(power->shutdownButton());
        QVERIFY(bar->value() > 0);
        QSignalSpy clicked(power->shutdownButton(), &QPushButton::clicked);
        QTest::mouseClick(power->shutdownButton(), Qt::LeftButton);
        QTRY_COMPARE(clicked.count(), 1);
        QTest::qWait(300);
        QCOMPARE(clicked.count(), 1);
    }
};

QTEST_MAIN(SettingsPageTests)
#include "test_settings_page.moc"
