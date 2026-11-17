#include "app_fixture.h"

#include "app/elided_label.h"
#include "app/home_page.h"
#include "app/signal_bars.h"

#include <QLabel>
#include <QScrollBar>
#include <QTest>

using namespace pi_control;
using namespace pi_control::app;
using State = wifi::WifiStatus::State;

namespace {

class ScriptedSampler final : public system::SystemStatsSampler {
public:
    int calls = 0;
    system::SystemStats sample() override {
        system::SystemStats stats{std::nullopt, 41.4, 2.8, 6'600'000'000, 235'000'000'000};
        if (calls++ > 0) stats.cpu = 23.2;
        return stats;
    }
};

struct HomeFixture : AppFixture {
    HomeFixture() : AppFixture(PowerOptions{.dryRun = true}, {}, makeServices()) {}
    static Services makeServices() {
        Services services;
        services.stats = std::make_unique<ScriptedSampler>();
        services.wifi = std::make_unique<wifi::FakeWifiBackend>();
        return services;
    }
    wifi::FakeWifiBackend& wifiStatus() const { return fakeWifi(); }
    HomePage* home() const { return page<HomePage>(u"home"); }
    QString text(const char* name) const { return home()->findChild<QLabel*>(name)->text(); }
};

} // namespace

class HomePageTests : public QObject {
    Q_OBJECT

private slots:
    void showsFormattedStats() {
        HomeFixture fixture;
        fixture.home()->setAddressSource([] {
            return std::vector<system::InterfaceAddress>{{"wlan0", "192.168.1.23"}};
        });
        fixture.home()->refresh();
        QCOMPARE(fixture.text("cpuValue"), QString("—"));
        QCOMPARE(fixture.text("memoryValue"), QString("41 %"));
        QCOMPARE(fixture.text("storageValue"), QString("3 %"));
        QCOMPARE(fixture.text("storageDetail"), QString("6.6 GB of 235 GB"));
        QCOMPARE(fixture.text("addressesLabel"), QString("wlan0  192.168.1.23"));
        fixture.home()->refresh();
        QCOMPARE(fixture.text("cpuValue"), QString("23 %"));
        QVERIFY(QRegularExpression("^\\d\\d:\\d\\d$").match(fixture.text("clockLabel")).hasMatch());
    }

    void noAddressSaysNoNetwork() {
        HomeFixture fixture;
        fixture.home()->setAddressSource([] { return std::vector<system::InterfaceAddress>{}; });
        QCOMPARE(fixture.text("addressesLabel"), QString("No network"));
    }

    void wifiStates() {
        HomeFixture fixture;
        fixture.wifiStatus().setStatus({.state = State::Connected, .ssid = "HomeNet", .strength = 72});
        QCOMPARE(fixture.text("wifiLabel"), QString("Wi‑Fi  HomeNet"));
        QCOMPARE(fixture.text("wifiStrengthLabel"), QString("72 %"));
        QCOMPARE(fixture.home()->findChild<SignalBars*>()->litBars(), 3);
        fixture.wifiStatus().setStatus({.state = State::Connecting, .ssid = "HomeNet"});
        QCOMPARE(fixture.text("wifiLabel"), QString("Wi‑Fi  Connecting to HomeNet…"));
        fixture.wifiStatus().setStatus({.state = State::RadioOff});
        QCOMPARE(fixture.text("wifiLabel"), QString("Wi‑Fi  Off"));
        fixture.wifiStatus().setStatus({.state = State::Disconnected});
        QCOMPARE(fixture.text("wifiLabel"), QString("Wi‑Fi  Not connected"));
        fixture.wifiStatus().setStatus({.state = State::Unavailable, .detail = "No Wi‑Fi adapter"});
        QCOMPARE(fixture.text("wifiLabel"), QString("Wi‑Fi  Unavailable"));
    }

    void signalBarThresholds() {
        QCOMPARE(SignalBars::barsFor(std::nullopt), 0);
        QCOMPARE(SignalBars::barsFor(0), 0);
        QCOMPARE(SignalBars::barsFor(1), 1);
        QCOMPARE(SignalBars::barsFor(25), 2);
        QCOMPARE(SignalBars::barsFor(50), 3);
        QCOMPARE(SignalBars::barsFor(75), 4);
    }

    void fitsAt800x480() {
        HomeFixture fixture;
        fixture.home()->setAddressSource([] {
            return std::vector<system::InterfaceAddress>{{"wlan0", "192.168.100.123"}, {"eth0", "10.0.0.2"}};
        });
        fixture.wifiStatus().setStatus({.state = State::Connected, .ssid = "A rather long network name", .strength = 72});
        fixture.shell->show();
        QVERIFY(QTest::qWaitForWindowExposed(fixture.shell.get()));
        fixture.home()->refresh();
        fixture.home()->refresh();
        QApplication::processEvents();
        const QRect pageRect = fixture.home()->rect();
        for (QLabel* label : fixture.home()->findChildren<QLabel*>()) {
            if (!label->isVisible()) continue;
            const QRect geometry(label->mapTo(fixture.home(), QPoint(0, 0)), label->size());
            QVERIFY2(pageRect.contains(geometry), qPrintable(label->objectName()));
            if (qobject_cast<ElidedLabel*>(label)) continue; // Elides by design.
            QVERIFY2(label->sizeHint().width() <= label->width(), qPrintable(label->objectName()));
        }
        QVERIFY(fixture.home()->findChildren<QScrollBar*>().isEmpty());
    }
};

QTEST_MAIN(HomePageTests)
#include "test_home_page.moc"
