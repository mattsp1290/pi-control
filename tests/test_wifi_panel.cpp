#include "app_fixture.h"

#include "app/password_view.h"
#include "app/settings_page.h"
#include "app/wifi_panel.h"
#include "keyboard/on_screen_keyboard.h"
#include "screensaver/screensaver_view.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTest>
#include <QToolButton>

using namespace pi_control;
using namespace pi_control::app;
using namespace std::chrono_literals;
using wifi::WifiError;
using wifi::WifiSecurity;
using State = wifi::WifiStatus::State;

namespace {

wifi::WifiNetwork network(const char* ssid, int strength, WifiSecurity security, bool saved = false) {
    wifi::WifiNetwork result;
    result.ssid = ssid;
    result.ssidBytes = ssid;
    result.strength = strength;
    result.security = security;
    result.saved = saved;
    return result;
}

constexpr int kTapSettleMs = 50;

struct PanelFixture : AppFixture {
    explicit PanelFixture(screensaver::ScreensaverControllerConfig saverConfig = idleForever())
        : AppFixture({.dryRun = true}, std::move(saverConfig)) {}

    static screensaver::ScreensaverControllerConfig idleForever() {
        screensaver::ScreensaverControllerConfig config;
        config.idleTimeout = 600s;
        return config;
    }

    WifiPanel* panel() const { return page<SettingsPage>(u"settings")->findChild<WifiPanel*>(); }

    // Shows Settings and waits for the automatic first scan.
    void open() {
        shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(shell.get()));
        showTab(u"settings");
        QTRY_VERIFY(!wifi->isScanning());
        QTest::qWait(kTapSettleMs); // Lay out the rows the scan rebuilt.
    }

    // Taps like a finger: the page's kinetic scroller holds a press back
    // briefly to tell taps from drags, so let it settle.
    void tap(QWidget* widget) {
        QVERIFY(widget);
        QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        page<SettingsPage>(u"settings")->scrollArea()->ensureWidgetVisible(widget);
        QTest::qWait(kTapSettleMs);
        QTest::mouseClick(widget, Qt::LeftButton, {}, QPoint(20, widget->height() / 2));
        QTest::qWait(kTapSettleMs);
    }

    // Opens the password view for ssid by tapping its row.
    PasswordView* openPassword(const QString& ssid) {
        tap(panel()->row(ssid));
        if (!view() || shell->currentTask() != view()) return nullptr;
        return view();
    }

    void tapKey(const QString& name) {
        QToolButton* key = panel()->passwordView()->keyboard()->key(name);
        QVERIFY2(key, qPrintable(name));
        QTest::mouseClick(key, Qt::LeftButton);
    }

    // Types lower-case letters and digits with key taps only.
    void type(const QString& text) {
        keyboard::OnScreenKeyboard* keyboard = panel()->passwordView()->keyboard();
        for (const QChar c : text) {
            const bool digit = c.isDigit();
            const bool lettersShown = keyboard->layout() == keyboard::OnScreenKeyboard::Layout::Letters;
            if (digit && lettersShown) tapKey("?123");
            if (!digit && !lettersShown) tapKey("ABC");
            tapKey(QString(c));
        }
    }

    PasswordView* view() const { return panel()->passwordView(); }
};

} // namespace

class WifiPanelTests : public QObject {
    Q_OBJECT

private slots:
    void rowsFollowBackendOrder() {
        PanelFixture fixture;
        fixture.open();
        QStringList ssids;
        for (NetworkRow* row : fixture.panel()->rows()) ssids << row->network().ssid;
        QCOMPARE(ssids, (QStringList{"HomeNet", "Neighbour5G", "CoffeeShop", "CorpNet", "Upstairs"}));
        NetworkRow* corp = fixture.panel()->row("CorpNet");
        QVERIFY(!corp->isEnabled());
        QCOMPARE(corp->stateLabel()->text(), QString("Enterprise – not supported"));
        QCOMPARE(fixture.panel()->headerLabel()->text(), QString("Connected to HomeNet"));
    }

    void joinSecuredNetworkWithKeyTaps() {
        PanelFixture fixture;
        fixture.open();
        fixture.tap(fixture.panel()->row("Neighbour5G"));
        PasswordView* view = fixture.view();
        QVERIFY(view);
        QCOMPARE(fixture.shell->currentTask(), view);
        QVERIFY(!fixture.shell->tabWidget()->isVisible());
        QVERIFY(view->keyboard()->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), view->field());
        fixture.type("password123");
        QCOMPARE(view->field()->text(), QString("password123"));
        QVERIFY(view->connectButton()->isEnabled());
        QTest::mouseClick(view->connectButton(), Qt::LeftButton);
        QTRY_COMPARE(fixture.shell->currentTask(), nullptr);
        QVERIFY(fixture.panel()->statusLine()->isVisible());
        QCOMPARE(fixture.panel()->statusLine()->text(), QString("Connected to Neighbour5G"));
        QCOMPARE(view->field()->text(), QString());
    }

    void wrongPasswordStaysOpen() {
        PanelFixture fixture;
        fixture.open();
        fixture.tap(fixture.panel()->row("Neighbour5G"));
        PasswordView* view = fixture.view();
        fixture.type("wrongpass1");
        QTest::mouseClick(view->connectButton(), Qt::LeftButton);
        QTRY_COMPARE(view->messageLabel()->text(), QString("⚠ Wrong password. Try again."));
        QCOMPARE(fixture.shell->currentTask(), view);
        QVERIFY(view->field()->isEnabled());
        QCOMPARE(view->field()->selectedText(), QString("wrongpass1"));
        fixture.type("password123"); // Replaces the selection.
        QCOMPARE(view->field()->text(), QString("password123"));
        fixture.tapKey("Done");
        QTRY_COMPARE(fixture.shell->currentTask(), nullptr);
        QCOMPARE(fixture.fakeWifi().connectCalls(), 2);
    }

    void connectNeedsAValidPassphrase() {
        PanelFixture fixture;
        fixture.open();
        fixture.tap(fixture.panel()->row("Neighbour5G"));
        PasswordView* view = fixture.view();
        view->field()->setText("1234567");
        QVERIFY(!view->connectButton()->isEnabled());
        QCOMPARE(view->messageLabel()->text(), QString("At least 8 characters"));
        view->field()->setText("12345678");
        QVERIFY(view->connectButton()->isEnabled());
        view->field()->setText(QString(64, 'a'));
        QVERIFY(view->connectButton()->isEnabled());
        view->field()->setText(QString(64, 'z'));
        QVERIFY(!view->connectButton()->isEnabled());
        view->field()->setText(QString::fromUtf8("passwé12"));
        QVERIFY(!view->connectButton()->isEnabled());
        QCOMPARE(view->messageLabel()->text(), QString("⚠ Use only letters, numbers and symbols from the keyboard."));
        fixture.tapKey("Done");
        QCOMPARE(fixture.shell->currentTask(), view);
        QCOMPARE(fixture.fakeWifi().connectCalls(), 0);
    }

    void savedPasswordRejected() {
        PanelFixture fixture;
        fixture.fakeWifi().setNetworks({network("HomeNet", 80, WifiSecurity::Personal, true),
                                        network("Cabin", 60, WifiSecurity::Personal, true)});
        fixture.open();
        fixture.fakeWifi().setNextError(WifiError::WrongPassword);
        fixture.tap(fixture.panel()->row("Cabin"));
        QTRY_VERIFY(fixture.shell->currentTask() != nullptr);
        QCOMPARE(fixture.view()->messageLabel()->text(),
                 QString("⚠ The saved password didn't work. Enter it again."));
    }

    void forgetNeedsTwoTapsAndNeverTheActiveNetwork() {
        PanelFixture fixture;
        fixture.fakeWifi().setNetworks({network("HomeNet", 80, WifiSecurity::Personal, true),
                                        network("Cabin", 60, WifiSecurity::Personal, true)});
        fixture.open();
        fixture.panel()->setForgetConfirmWindow(300ms);
        QVERIFY(fixture.panel()->row("HomeNet")->network().active);
        QCOMPARE(fixture.panel()->row("HomeNet")->forgetButton(), nullptr);
        QPushButton* forget = fixture.panel()->row("Cabin")->forgetButton();
        QVERIFY(forget);
        fixture.tap(forget);
        QCOMPARE(forget->text(), QString("Tap again to forget"));
        QTest::qWait(400);
        QCOMPARE(forget->text(), QString("Forget"));
        QCOMPARE(fixture.fakeWifi().forgetCalls(), 0);
        fixture.tap(forget);
        fixture.tap(forget);
        QCOMPARE(fixture.fakeWifi().forgetCalls(), 1);
        QCOMPARE(fixture.fakeWifi().connectCalls(), 0); // The tap was not also a row tap.
        QTRY_COMPARE(fixture.panel()->statusLine()->text(), QString("Forgot Cabin"));
        QTRY_COMPARE(fixture.panel()->row("Cabin")->forgetButton(), nullptr);
    }

    void armedForgetSurvivesListUpdates() {
        PanelFixture fixture;
        fixture.fakeWifi().setNetworks({network("HomeNet", 80, WifiSecurity::Personal, true),
                                        network("Cabin", 60, WifiSecurity::Personal, true)});
        fixture.open();
        NetworkRow* cabin = fixture.panel()->row("Cabin");
        fixture.tap(cabin->forgetButton());
        QCOMPARE(cabin->forgetButton()->text(), QString("Tap again to forget"));
        // A scan finds another network while Forget is armed.
        fixture.fakeWifi().setNetworks({network("HomeNet", 80, WifiSecurity::Personal, true),
                                        network("Cabin", 60, WifiSecurity::Personal, true),
                                        network("Attic", 30, WifiSecurity::Open)});
        QCOMPARE(fixture.panel()->row("Cabin"), cabin);
        fixture.tap(cabin->forgetButton());
        QCOMPARE(fixture.fakeWifi().forgetCalls(), 1);
        QTRY_VERIFY(fixture.panel()->row("Attic") != nullptr);
    }

    void passwordResubmittedDuringRecovery() {
        PanelFixture fixture;
        fixture.open();
        fixture.fakeWifi().setRecoveryDelay(400ms);
        fixture.tap(fixture.panel()->row("Neighbour5G"));
        fixture.view()->field()->setText("wrongpass1");
        QTest::mouseClick(fixture.view()->connectButton(), Qt::LeftButton);
        QTRY_COMPARE(fixture.view()->messageLabel()->text(), QString("⚠ Wrong password. Try again."));
        QVERIFY(fixture.wifi->isConnecting()); // Still returning to the previous network.
        // The failed row no longer claims to be connecting.
        QCOMPARE(fixture.panel()->row("Neighbour5G")->stateLabel()->text(), QString());
        fixture.view()->field()->setText("password123");
        QTest::mouseClick(fixture.view()->connectButton(), Qt::LeftButton);
        QCOMPARE(fixture.fakeWifi().connectCalls(), 1); // Waits for recovery.
        QVERIFY(fixture.view()->isConnecting());
        QTRY_COMPARE(fixture.fakeWifi().connectCalls(), 2);
        QTRY_COMPARE(fixture.shell->currentTask(), nullptr);
        QCOMPARE(fixture.panel()->statusLine()->text(), QString("Connected to Neighbour5G"));
    }

    void rowsLockedWhileConnecting() {
        PanelFixture fixture;
        fixture.open();
        fixture.fakeWifi().setConnectDelay(500ms);
        fixture.tap(fixture.panel()->row("CoffeeShop"));
        QCOMPARE(fixture.fakeWifi().connectCalls(), 1);
        QTRY_COMPARE(fixture.panel()->row("CoffeeShop")->stateLabel()->text(), QString("Connecting…"));
        QVERIFY(!fixture.panel()->row("Upstairs")->isEnabled());
        fixture.tap(fixture.panel()->row("Upstairs"));
        QCOMPARE(fixture.fakeWifi().connectCalls(), 1);
        QCOMPARE(fixture.shell->currentTask(), nullptr);
        QTRY_COMPARE(fixture.panel()->statusLine()->text(), QString("Connected to CoffeeShop"));
        QTRY_VERIFY(fixture.panel()->row("Upstairs")->isEnabled());
    }

    void scanButtonReflectsScanning() {
        PanelFixture fixture;
        fixture.open();
        QCOMPARE(fixture.panel()->lastScanLabel()->text().left(9), QString("Last scan"));
        fixture.fakeWifi().setScanDelay(200ms);
        fixture.tap(fixture.panel()->scanButton());
        QVERIFY(!fixture.panel()->scanButton()->isEnabled());
        QCOMPARE(fixture.panel()->scanButton()->text(), QString("Scanning…"));
        QTRY_VERIFY(fixture.panel()->scanButton()->isEnabled());
        const QString lastScan = fixture.panel()->lastScanLabel()->text();
        fixture.fakeWifi().setScanError(WifiError::NotPermitted);
        fixture.tap(fixture.panel()->scanButton());
        QTRY_VERIFY(fixture.panel()->scanButton()->isEnabled());
        QCOMPARE(fixture.panel()->statusLine()->text(),
                 QString("Pi Control isn't allowed to scan. See the service journal."));
        QCOMPARE(fixture.panel()->lastScanLabel()->text(), lastScan);
    }

    void unavailableShowsWhy() {
        PanelFixture fixture;
        fixture.open();
        fixture.fakeWifi().setStatus({.state = State::Unavailable, .detail = "No Wi‑Fi adapter"});
        QCOMPARE(fixture.panel()->headerLabel()->text(), QString("Wi‑Fi unavailable — No Wi‑Fi adapter"));
        QVERIFY(!fixture.panel()->scanButton()->isEnabled());
        QVERIFY(fixture.panel()->rows().empty());
    }

    void vanishDuringConnect() {
        PanelFixture fixture;
        fixture.open();
        fixture.tap(fixture.panel()->row("Neighbour5G"));
        fixture.fakeWifi().setConnectDelay(500ms);
        fixture.view()->field()->setText("password123");
        QTest::mouseClick(fixture.view()->connectButton(), Qt::LeftButton);
        QVERIFY(!fixture.view()->field()->isEnabled());
        fixture.fakeWifi().vanish();
        QTRY_COMPARE(fixture.view()->messageLabel()->text(), QString("⚠ Couldn't connect. Try again."));
        QVERIFY(fixture.view()->field()->isEnabled());
    }

    void passwordViewFitsWithoutScrolling() {
        PanelFixture fixture;
        fixture.open();
        fixture.tap(fixture.panel()->row("Neighbour5G"));
        PasswordView* view = fixture.view();
        QApplication::processEvents();
        QCOMPARE(fixture.shell->size(), QSize(800, 480));
        std::vector<QWidget*> controls{view->field(), view->showButton(), view->cancelButton(), view->connectButton()};
        for (QToolButton* key : view->keyboard()->findChildren<QToolButton*>()) {
            if (key->isVisible()) controls.push_back(key);
        }
        const QRect window = fixture.shell->rect();
        std::vector<QRect> rects;
        for (QWidget* control : controls) {
            const QRect rect(control->mapTo(fixture.shell.get(), QPoint(0, 0)), control->size());
            QVERIFY2(window.contains(rect), qPrintable(control->objectName() + control->accessibleName()));
            for (const QRect& other : rects) QVERIFY(!rect.intersects(other));
            rects.push_back(rect);
        }
        for (QLabel* label : view->findChildren<QLabel*>()) {
            if (label->isVisible() && !label->wordWrap()) {
                QVERIFY2(label->sizeHint().width() <= label->width(), qPrintable(label->text()));
            }
        }
    }

    void screensaverConcealsPasswordAndWakeTapTypesNothing() {
        screensaver::ScreensaverControllerConfig config;
        config.idleTimeout = 400ms;
        config.wakeGuard = 100ms;
        PanelFixture fixture(std::move(config));
        // Hold the screensaver off while setting up, so a slow machine cannot
        // start it before the password is shown.
        fixture.controller->setInhibited(true);
        fixture.open();
        fixture.tap(fixture.panel()->row("Neighbour5G"));
        fixture.type("abc");
        QTest::mouseClick(fixture.view()->showButton(), Qt::LeftButton);
        QCOMPARE(fixture.view()->field()->echoMode(), QLineEdit::Normal);
        fixture.controller->setInhibited(false);
        QTRY_VERIFY_WITH_TIMEOUT(fixture.controller->isActive(), 3000);
        QCOMPARE(fixture.view()->field()->echoMode(), QLineEdit::Password);
        QToolButton* d = fixture.view()->keyboard()->key("d");
        QTest::mouseClick(fixture.shell->windowHandle(), Qt::LeftButton, {},
                          d->mapTo(fixture.shell.get(), d->rect().center()));
        QVERIFY(!fixture.controller->isActive());
        QCOMPARE(fixture.view()->field()->text(), QString("abc"));
        QTest::qWait(300);
        fixture.tapKey("d");
        QCOMPARE(fixture.view()->field()->text(), QString("abcd"));
    }

    void screensaverStaysOnTopOfResults() {
        PanelFixture fixture;
        fixture.open();
        fixture.fakeWifi().setConnectDelay(300ms);
        fixture.tap(fixture.panel()->row("CoffeeShop"));
        fixture.controller->preview();
        QVERIFY(fixture.controller->isActive());
        QTRY_COMPARE(fixture.panel()->statusLine()->text(), QString("Connected to CoffeeShop"));
        QObject* last = fixture.shell->children().last();
        QCOMPARE(last, static_cast<QObject*>(&fixture.shell->screensaverView()));
        QVERIFY(fixture.shell->screensaverView().isVisible());
    }
};

QTEST_MAIN(WifiPanelTests)
#include "test_wifi_panel.moc"
