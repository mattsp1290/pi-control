#pragma once

#include <QString>

namespace pi_control::app {

// Persistent user choices in an INI file.
class SettingsStore {
public:
    explicit SettingsStore(QString filePath);

    // $STATE_DIRECTORY/settings.ini under systemd (the first entry if it
    // lists several), otherwise the per-user config directory.
    static QString defaultPath();

    QString filePath() const { return filePath_; }
    // Empty when nothing is saved.
    QString screensaverId() const;
    // False when the file could not be written.
    bool setScreensaverId(const QString& id);

private:
    QString filePath_;
};

} // namespace pi_control::app
