#include "app_fixture.h"
#include "fake_update_starter.h"
#include "release_tree.h"

#include "app/power_panel.h"
#include "app/screensaver_panel.h"
#include "app/settings_page.h"
#include "app/update_panel.h"
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
        QCOMPARE(settings->sectionTitles(), (QStringList{"Wi‑Fi", "Screensaver", "Software update", "Power"}));
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
        // Choosing the override in Settings saves it.
        emit screensaverPanel(fixture)->choices()->idClicked(1);
        QCOMPARE(fixture.settings->screensaverId(), QString("mandelbrot"));
    }

    void updateButtonAppearsOnlyWithANewerRelease() {
        ReleaseTree tree("0.3.0");
        AppFixture::Services services;
        services.updater = std::make_unique<update::Updater>(
            update::UpdaterOptions{.current = {0, 2, 0}, .releaseBase = tree.base()},
            std::make_unique<update::DryRunUpdateStarter>());
        AppFixture fixture({.dryRun = true}, {}, std::move(services));
        fixture.shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(fixture.shell.get()));
        fixture.showTab(u"settings");
        auto* panel = fixture.page<SettingsPage>(u"settings")->findChild<UpdatePanel*>();
        QVERIFY(panel);
        QCOMPARE(panel->statusLabel()->text(), QString("Version 0.2.0."));
        QVERIFY(panel->updateButton()->isHidden());

        fixture.page<SettingsPage>(u"settings")->scrollArea()->ensureWidgetVisible(panel->checkButton());
        QTest::mouseClick(panel->checkButton(), Qt::LeftButton);
        QTRY_COMPARE(panel->statusLabel()->text(), QString("Version 0.3.0 is available. This is version 0.2.0."));
        QVERIFY(!panel->updateButton()->isHidden());
        QCOMPARE(panel->updateButton()->text(), QString("Update to 0.3.0"));

        // A re-check keeps the known release on offer.
        fixture.updater->check();
        QVERIFY(!panel->updateButton()->isHidden());
        QVERIFY(!panel->updateButton()->isEnabled());
        QTRY_VERIFY(panel->updateButton()->isEnabled());

        fixture.page<SettingsPage>(u"settings")->scrollArea()->ensureWidgetVisible(panel->updateButton());
        QTest::mouseClick(panel->updateButton(), Qt::LeftButton);
        QCOMPARE(panel->statusLabel()->text(), QString("Dry run: update to 0.3.0 requested."));
        QVERIFY(!panel->updateButton()->isEnabled());
        QVERIFY(!panel->checkButton()->isEnabled());
        QVERIFY(fixture.controller->isInhibited());
    }

    void upToDateHidesTheUpdateButton() {
        ReleaseTree tree("0.2.0");
        AppFixture::Services services;
        services.updater = std::make_unique<update::Updater>(
            update::UpdaterOptions{.current = {0, 2, 0}, .releaseBase = tree.base()},
            std::make_unique<update::DryRunUpdateStarter>());
        AppFixture fixture({.dryRun = true}, {}, std::move(services));
        auto* panel = fixture.page<SettingsPage>(u"settings")->findChild<UpdatePanel*>();
        fixture.updater->check();
        QTRY_COMPARE(panel->statusLabel()->text(), QString("Version 0.2.0 is up to date."));
        QVERIFY(panel->updateButton()->isHidden());
        QVERIFY(panel->checkButton()->isEnabled());
    }

    // Installing blocks Shut down and the screensaver; each panel releases
    // only its own screensaver inhibition.
    void updateAndShutdownDoNotReleaseEachOther() {
        ReleaseTree tree("0.3.0");
        auto starter = std::make_unique<FakeUpdateStarter>();
        FakeUpdateStarter* fake = starter.get();
        AppFixture::Services services;
        services.updater = std::make_unique<update::Updater>(
            update::UpdaterOptions{.current = {0, 2, 0}, .releaseBase = tree.base()}, std::move(starter));
        // Not a dry run, but shutdown goes to a disconnected bus and fails.
        AppFixture fixture({.dryRun = false, .failureTest = true}, {}, std::move(services));
        auto* settings = fixture.page<SettingsPage>(u"settings");
        auto* panel = settings->findChild<UpdatePanel*>();
        PowerPanel* power = settings->powerPanel();
        fixture.updater->check();
        QTRY_VERIFY(panel->updateButton()->isEnabled() && !panel->updateButton()->isHidden());

        // A shutdown that fails while the update installs keeps the inhibition.
        power->shutdownButton()->click();
        panel->updateButton()->click();
        QCOMPARE(fake->starts, 1);
        QCOMPARE(panel->statusLabel()->text(),
                 QString("Updating to the latest release (0.3.0 or newer)… The screen restarts when it's done."));
        QTRY_VERIFY(power->statusLabel()->text().startsWith("Shutdown failed"));
        QVERIFY(fixture.controller->isInhibited());
        QVERIFY(!power->shutdownButton()->isEnabled());
        power->shutdownButton()->click(); // Disabled: no request.

        fake->finish(update::UpdateStarter::Result::Failed, "failed");
        QCOMPARE(panel->statusLabel()->text(),
                 QString("Update failed. Check journalctl -u pi-control-update and try again."));
        QVERIFY(panel->updateButton()->isEnabled());
        QVERIFY(power->shutdownButton()->isEnabled());
        QVERIFY(!fixture.controller->isInhibited());

        // Blocking shows why, and unblocking restores the panel.
        panel->updateButton()->click();
        QCOMPARE(power->statusLabel()->text(), QString("Shut down is unavailable while the software update installs."));
        fake->finish(update::UpdateStarter::Result::Done);
        QCOMPARE(panel->statusLabel()->text(),
                 QString("The installed package is already the latest release. Restart Pi Control to run it."));
        QVERIFY(panel->updateButton()->isHidden());
        QCOMPARE(power->statusLabel()->text(), QString());
        QVERIFY(power->shutdownButton()->isEnabled());
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
