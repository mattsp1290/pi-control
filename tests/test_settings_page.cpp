#include "app_fixture.h"

#include "app/power_panel.h"
#include "app/screensaver_panel.h"
#include "app/settings_page.h"
#include "screensaver/registry.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

using namespace pi_control;
using namespace pi_control::app;

namespace {

ScreensaverPanel* screensaverPanel(const AppFixture& fixture) {
    return fixture.page<SettingsPage>(u"settings")->findChild<ScreensaverPanel*>();
}

void clickChoice(ScreensaverPanel* panel, const char* id) {
    auto* choice = panel->findChild<QAbstractButton*>(id);
    QVERIFY(choice);
    QTest::mouseClick(choice, Qt::LeftButton, {}, QPoint(20, choice->height() / 2));
}

} // namespace

class SettingsPageTests : public QObject {
    Q_OBJECT

private slots:
    void sectionsInOrder() {
        AppFixture fixture;
        auto* settings = fixture.page<SettingsPage>(u"settings");
        QCOMPARE(settings->sectionTitles(), (QStringList{"Screensaver", "Power"}));
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

    void oneChoicePerScreensaver() {
        AppFixture fixture;
        ScreensaverPanel* panel = screensaverPanel(fixture);
        QVERIFY(panel);
        const auto& screensavers = screensaver::builtinScreensavers();
        QCOMPARE(panel->choices()->buttons().size(), qsizetype(screensavers.size()));
        for (std::size_t i = 0; i < screensavers.size(); ++i) {
            QCOMPARE(panel->choices()->button(int(i))->text(), screensavers[i].displayName);
        }
        QCOMPARE(panel->choices()->checkedId(), 0); // The default, without saving it.
        QCOMPARE(fixture.settings->screensaverId(), QString());
    }

    void choosingSavesAndRestores() {
        AppFixture fixture;
        fixture.shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(fixture.shell.get()));
        fixture.showTab(u"settings");
        ScreensaverPanel* panel = screensaverPanel(fixture);
        panel->findChild<QAbstractButton*>("water")->setFocus();
        auto* settings = fixture.page<SettingsPage>(u"settings");
        settings->scrollArea()->ensureWidgetVisible(panel->findChild<QAbstractButton*>("water"));
        clickChoice(panel, "water");
        QCOMPARE(fixture.controller->screensaverId(), QString("water"));
        QFile file(fixture.settings->filePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY2(file.readAll().contains("id=water"), qPrintable(fixture.settings->filePath()));

        AppFixture reopened({.dryRun = true}, {}, AppFixture::withSettingsFile(fixture.settings->filePath()));
        reopened.controller->setScreensaver(*screensaver::findScreensaver(reopened.settings->screensaverId()));
        // A new panel reads the controller, which main() sets from the store.
        ScreensaverPanel fresh(*reopened.controller, *reopened.settings);
        QCOMPARE(fresh.choices()->checkedButton()->objectName(), QString("water"));
    }

    void commandLineChoiceIsNotSaved() {
        screensaver::ScreensaverControllerConfig config;
        config.screensaver = screensaver::findScreensaver(u"mandelbrot");
        AppFixture fixture({.dryRun = true}, std::move(config));
        QCOMPARE(screensaverPanel(fixture)->choices()->checkedButton()->objectName(), QString("mandelbrot"));
        QCOMPARE(fixture.settings->screensaverId(), QString());
    }

    void saveFailureKeepsChoiceForSession() {
        QTemporaryDir dir;
        QFile blocker(dir.filePath("file"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        AppFixture fixture({.dryRun = true}, {}, AppFixture::withSettingsFile(dir.filePath("file/settings.ini")));
        fixture.shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(fixture.shell.get()));
        fixture.showTab(u"settings");
        ScreensaverPanel* panel = screensaverPanel(fixture);
        fixture.page<SettingsPage>(u"settings")->scrollArea()->ensureWidgetVisible(
            panel->findChild<QAbstractButton*>("water"));
        clickChoice(panel, "water");
        QCOMPARE(fixture.controller->screensaverId(), QString("water"));
        QVERIFY(panel->statusLabel()->isVisible());
        QCOMPARE(panel->statusLabel()->text(), QString("Couldn't save. The choice lasts until restart."));
    }
};

QTEST_MAIN(SettingsPageTests)
#include "test_settings_page.moc"
