#include "app/settings_store.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace pi_control::app;

class SettingsStoreTests : public QObject {
    Q_OBJECT

private slots:
    void roundTrip() {
        QTemporaryDir dir;
        const QString path = dir.filePath("nested/settings.ini");
        QVERIFY(SettingsStore(path).setScreensaverId("water"));
        QCOMPARE(SettingsStore(path).screensaverId(), QString("water"));
    }

    void unsetIsEmpty() {
        QTemporaryDir dir;
        QCOMPARE(SettingsStore(dir.filePath("settings.ini")).screensaverId(), QString());
    }

    void stateDirectoryWins() {
        qputenv("STATE_DIRECTORY", "/a:/b");
        QCOMPARE(SettingsStore::defaultPath(), QString("/a/settings.ini"));
        qputenv("STATE_DIRECTORY", "/var/lib/pi-control");
        QCOMPARE(SettingsStore::defaultPath(), QString("/var/lib/pi-control/settings.ini"));
        qunsetenv("STATE_DIRECTORY");
        QVERIFY(SettingsStore::defaultPath().endsWith("/settings.ini"));
        QVERIFY(!SettingsStore::defaultPath().startsWith("/a/"));
    }

    void writeFailureIsReported() {
        // A parent that is a regular file fails for root too.
        QTemporaryDir dir;
        QFile blocker(dir.filePath("file"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        SettingsStore store(dir.filePath("file/settings.ini"));
        QVERIFY(!store.setScreensaverId("water"));
        QCOMPARE(store.screensaverId(), QString());
    }
};

QTEST_MAIN(SettingsStoreTests)
#include "test_settings_store.moc"
