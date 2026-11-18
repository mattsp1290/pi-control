#pragma once

#include "update/release.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <chrono>

class QDBusObjectPath;
class QNetworkReply;

namespace pi_control::update {

inline constexpr char kUpdateUnit[] = "pi-control-update.service";

struct UpdaterOptions {
    Version current;
    // Holds <base>/latest/download/pi-control-manifest.json. https:// or file://.
    QUrl releaseBase{"https://github.com/mattsp1290/pi-control/releases"};
    bool dryRun = false;      // install() is simulated; nothing is started.
    bool failureTest = false; // install() goes to a disconnected bus, so it always fails.
};

// Finds out whether a newer release is published, and installs it by starting
// the packaged pi-control-update.service. That unit runs the release installer
// as root; on success the package restarts pi-control, replacing this process.
class Updater final : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Checking, UpToDate, Available, CheckFailed, Installing, InstallFailed };
    Q_ENUM(State)

    explicit Updater(UpdaterOptions options, QObject* parent = nullptr);

    State state() const { return state_; }
    Version current() const { return options_.current; }
    // The newest published release from the last successful check.
    std::optional<Version> latest() const { return latest_; }
    bool isDryRun() const { return options_.dryRun && !options_.failureTest; }

    // Checks now, then every interval. A check due while installing is skipped.
    void startPeriodicChecks(std::chrono::milliseconds first, std::chrono::milliseconds interval);
    // Does nothing while checking or installing.
    void check();
    // Installs the latest release. Only acts when an update is available, or
    // after a failed install.
    void install();

signals:
    void stateChanged(pi_control::update::Updater::State state);
    void dryRunInstallRequested();

private slots:
    void onJobRemoved(uint id, const QDBusObjectPath& job, const QString& unit, const QString& result);

private:
    void setState(State state);
    void finishCheck(QNetworkReply* reply, bool tooLarge);
    void installFailed(const QString& detail);

    UpdaterOptions options_;
    State state_ = State::Idle;
    std::optional<Version> latest_;
    QNetworkAccessManager network_;
    QTimer periodic_;
    std::chrono::milliseconds interval_{0};
    bool subscribed_ = false;
};

} // namespace pi_control::update
