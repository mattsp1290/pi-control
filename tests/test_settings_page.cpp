#include "app_fixture.h"

#include "app/power_panel.h"
#include "app/screensaver_panel.h"
#include "app/settings_page.h"
#include "app/wifi_panel.h"
#include "screensaver/registry.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

using namespace pi_control;
using namespace pi_control::app;

namespace {

ScreensaverPanel* screensaverPanel(const AppFixture& fixture) {
    return fixture.page<SettingsPage>(u"settings")->findChild<ScreensaverPanel*>();
}

void showWithManyNetworks(AppFixture& fixture) {
    std::vector<wifi::WifiNetwork> networks;
    for (int i = 0; i < 15; ++i) {
        wifi::WifiNetwork network;
        network.ssid = QString("Network %1").arg(i, 2, 10, QChar('0'));
        network.ssidBytes = network.ssid.toUtf8();
        network.strength = 90 - i;
        network.security = wifi::WifiSecurity::Open;
        networks.push_back(network);
    }
    fixture.fakeWifi().setStatus({.state = wifi::WifiStatus::State::Disconnected});
    fixture.fakeWifi().setNetworks(networks);
    fixture.shell->show();
    QVERIFY(QTest::qWaitForWindowExposed(fixture.shell.get()));
    fixture.showTab(u"settings");
    QTRY_VERIFY(!fixture.wifi->isScanning());
    QTest::qWait(50);
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
        QCOMPARE(settings->sectionTitles(), (QStringList{"Wi‑Fi", "Screensaver", "Power"}));
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

    void dragOnNetworkRowScrollsPage() {
        AppFixture fixture;
        showWithManyNetworks(fixture);
        auto* settings = fixture.page<SettingsPage>(u"settings");
        auto* panel = settings->findChild<WifiPanel*>();
        QCOMPARE(panel->rows().size(), std::size_t(15));
        QScrollBar* bar = settings->scrollArea()->verticalScrollBar();
        QCOMPARE(bar->value(), 0);
        NetworkRow* row = panel->rows()[2];
        QWidget* viewport = settings->scrollArea()->viewport();
        QWindow* window = fixture.shell->windowHandle();
        QPoint at = row->mapTo(fixture.shell.get(), QPoint(100, row->height() / 2));
        QTest::mousePress(window, Qt::LeftButton, {}, at);
        for (int i = 0; i < 15; ++i) {
            QTest::qWait(16);
            at.ry() -= 16;
            QTest::mouseMove(window, at);
        }
        QTest::mouseRelease(window, Qt::LeftButton, {}, at);
        QTRY_VERIFY(bar->value() > 0);
        QCOMPARE(fixture.fakeWifi().connectCalls(), 0); // A drag is not a tap.
        // Power stays reachable at the bottom.
        QScroller::scroller(viewport)->stop();
        bar->setValue(bar->maximum());
        QApplication::processEvents();
        PowerPanel* power = settings->powerPanel();
        const QRect shown(power->shutdownButton()->mapTo(viewport, QPoint(0, 0)), power->shutdownButton()->size());
        QVERIFY(viewport->rect().contains(shown));
    }

    void tapOnRowWhileScrolledActsOnce() {
        AppFixture fixture;
        showWithManyNetworks(fixture);
        auto* settings = fixture.page<SettingsPage>(u"settings");
        auto* panel = settings->findChild<WifiPanel*>();
        QScrollBar* bar = settings->scrollArea()->verticalScrollBar();
        bar->setValue(bar->maximum() / 3);
        QApplication::processEvents();
        NetworkRow* row = panel->rows()[6];
        settings->scrollArea()->ensureWidgetVisible(row);
        QVERIFY(bar->value() > 0);
        QTest::mouseClick(row, Qt::LeftButton, {}, QPoint(100, row->height() / 2));
        QTRY_COMPARE(fixture.fakeWifi().connectCalls(), 1);
        QTest::qWait(300);
        QCOMPARE(fixture.fakeWifi().connectCalls(), 1);
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
