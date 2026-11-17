#include "app/settings_store.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace pi_control::app {

namespace {

constexpr auto kScreensaverKey = "screensaver/id";

} // namespace

SettingsStore::SettingsStore(QString filePath) : filePath_(std::move(filePath)) {}

QString SettingsStore::defaultPath() {
    const QString stateDirectories = qEnvironmentVariable("STATE_DIRECTORY");
    if (!stateDirectories.isEmpty()) return stateDirectories.section(':', 0, 0) + "/settings.ini";
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.ini";
}

QString SettingsStore::screensaverId() const {
    const QSettings settings(filePath_, QSettings::IniFormat);
    return settings.value(kScreensaverKey).toString();
}

bool SettingsStore::setScreensaverId(const QString& id) {
    // QSettings creates missing directories but reports no error when a
    // parent path is a file, so check that first.
    const QFileInfo parent(QFileInfo(filePath_).absolutePath());
    if (parent.exists() && !parent.isDir()) return false;
    if (!parent.exists() && !QDir().mkpath(parent.absoluteFilePath())) return false;
    QSettings settings(filePath_, QSettings::IniFormat);
    settings.setValue(kScreensaverKey, id);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

} // namespace pi_control::app
