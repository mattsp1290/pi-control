#pragma once

#include "update/release.h"

#include <QDBusConnection>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <memory>
#include <optional>

class QDBusObjectPath;
class QNetworkReply;

namespace pi_control::update {

inline constexpr char kUpdateUnit[] = "pi-control-update.service";

// Starts pi-control-update.service and reports how its start job ended.
class UpdateStarter : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    // Emits finished() exactly once per call, unless it never hears back.
    virtual void start() = 0;

signals:
    // ok: the job ran to completion. Otherwise detail says why it did not.
    void finished(bool ok, const QString& detail);
};

// Starts the unit through systemd's D-Bus API and follows its start job.
class SystemdUpdateStarter final : public UpdateStarter {
    Q_OBJECT

public:
    explicit SystemdUpdateStarter(QDBusConnection bus, QObject* parent = nullptr);
    void start() override;

private slots:
    void onJobRemoved(uint id, const QDBusObjectPath& job, const QString& unit, const QString& result);

private:
    void finish(bool ok, const QString& detail);

    QDBusConnection bus_;
    bool running_ = false;
    QString job_; // From StartUnit's reply; empty until it arrives.
    // A JobRemoved for the unit that arrived before StartUnit's reply.
    std::optional<std::pair<QString, QString>> early_;
};

struct UpdaterOptions {
    Version current;
    // Holds <base>/latest/download/pi-control-manifest.json. https:// or file://.
    // Keep in step with base= in distribution/install.sh.
    QUrl releaseBase{"https://github.com/mattsp1290/pi-control/releases"};
    bool dryRun = false; // install() is simulated; nothing is started.
    // Longer than the unit's TimeoutStartSec: a job that has not ended by now
    // will not be reported.
    std::chrono::milliseconds installTimeout = std::chrono::minutes(21);
    std::chrono::milliseconds dryRunDuration = std::chrono::seconds(3);
};

// Finds out whether a newer release is published, and installs it by starting
// the packaged pi-control-update.service. That unit runs the release installer
// as root; on success the package restarts pi-control, replacing this process.
class Updater final : public QObject {
    Q_OBJECT

public:
    enum class State {
        Idle,
        Checking,
        UpToDate,
        Available,
        CheckFailed,
        Installing,
        InstallFailed,
        NothingInstalled, // The updater ran but found nothing newer than the package.
    };
    Q_ENUM(State)

    // starter may be null only for a dry run.
    Updater(UpdaterOptions options, std::unique_ptr<UpdateStarter> starter, QObject* parent = nullptr);

    State state() const { return state_; }
    Version current() const { return options_.current; }
    // The newest published release from the last successful check.
    std::optional<Version> latest() const { return latest_; }
    // A successful check has found a release newer than this build.
    bool newerKnown() const { return latest_ && *latest_ > options_.current; }
    bool canInstall() const;
    bool isDryRun() const { return options_.dryRun; }

    // Checks after first, then every interval. A check due while installing is skipped.
    void startPeriodicChecks(std::chrono::milliseconds first, std::chrono::milliseconds interval);
    // Does nothing while checking or installing.
    void check();
    // Installs the latest release when canInstall().
    void install();

signals:
    void stateChanged(pi_control::update::Updater::State state);
    void dryRunInstallRequested();

private:
    void setState(State state);
    void finishCheck(QNetworkReply* reply, bool tooLarge);
    void installFinished(bool ok, const QString& detail);

    UpdaterOptions options_;
    std::unique_ptr<UpdateStarter> starter_;
    State state_ = State::Idle;
    std::optional<Version> latest_;
    QNetworkAccessManager network_;
    QTimer periodic_;
    std::chrono::milliseconds interval_{0};
    QTimer installTimer_; // Watchdog in a real install; the simulation in a dry run.
};

} // namespace pi_control::update
