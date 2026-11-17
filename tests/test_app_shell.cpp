#include "app_fixture.h"

#include "app/example_page.h"
#include "app/power_panel.h"
#include "app/settings_page.h"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTest>

using namespace pi_control::app;

class AppShellTests : public QObject {
    Q_OBJECT

private slots:
    void tabsInOrderStartingOnHome() {
        AppFixture fixture;
        QTabWidget* tabs = fixture.shell->tabWidget();
        QStringList titles;
        for (int i = 0; i < tabs->count(); ++i) titles << tabs->tabText(i);
        QCOMPARE(titles, (QStringList{"Home", "Example", "Settings"}));
        QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(tabs->currentWidget(), fixture.shell->page(u"home"));
    }

    void tabsAreTouchSized() {
        AppFixture fixture;
        fixture.shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(fixture.shell.get()));
        QTabBar* bar = fixture.shell->tabWidget()->tabBar();
        for (int i = 0; i < bar->count(); ++i) {
            QVERIFY2(bar->tabRect(i).height() >= 64, qPrintable(QString::number(bar->tabRect(i).height())));
        }
        // Three equal tabs span the window.
        QVERIFY(bar->tabRect(2).right() >= fixture.shell->width() - 2);
    }

    void examplePageShowsLoremIpsum() {
        AppFixture fixture;
        auto* example = fixture.page<ExamplePage>(u"example");
        QVERIFY(example);
        auto* body = example->findChild<QLabel*>("exampleBody");
        QVERIFY(body);
        QVERIFY(body->text().startsWith("Lorem ipsum"));
    }

    void settingsEndsWithPower() {
        AppFixture fixture;
        auto* settings = fixture.page<SettingsPage>(u"settings");
        QVERIFY(settings);
        QCOMPARE(settings->lastSection(), settings->powerPanel());
        QVERIFY(settings->powerPanel()->shutdownButton()->isEnabled());
    }

    void taskLayerCoversTabs() {
        AppFixture fixture;
        fixture.shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(fixture.shell.get()));
        auto* task = new QWidget;
        fixture.shell->showTask(task);
        QCOMPARE(fixture.shell->currentTask(), task);
        QVERIFY(task->isVisible());
        QVERIFY(!fixture.shell->tabWidget()->isVisible());
        fixture.shell->closeTask();
        QCOMPARE(fixture.shell->currentTask(), nullptr);
        QVERIFY(fixture.shell->tabWidget()->isVisible());
        QVERIFY(!task->isVisible());
    }

    void dryRunShutdownInhibitsScreensaver() {
        AppFixture fixture;
        PowerPanel* power = fixture.page<SettingsPage>(u"settings")->powerPanel();
        QSignalSpy requested(power, &PowerPanel::shutdownRequested);
        power->shutdownButton()->click();
        QCOMPARE(requested.count(), 1);
        QVERIFY(!power->shutdownButton()->isEnabled());
        QCOMPARE(power->statusLabel()->text(), QString("Dry run: shutdown requested"));
        QVERIFY(fixture.controller->isInhibited());
    }

    void failedShutdownRecovers() {
        AppFixture fixture(PowerOptions{.dryRun = false, .failureTest = true});
        PowerPanel* power = fixture.page<SettingsPage>(u"settings")->powerPanel();
        QSignalSpy failed(power, &PowerPanel::shutdownFailed);
        power->shutdownButton()->click();
        QVERIFY(fixture.controller->isInhibited());
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(power->shutdownButton()->isEnabled());
        QVERIFY(power->statusLabel()->text().startsWith("Shutdown failed"));
        QVERIFY(!fixture.controller->isInhibited());
    }
};

QTEST_MAIN(AppShellTests)
#include "test_app_shell.moc"
