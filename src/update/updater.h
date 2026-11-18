#pragma once

#include "update/release.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <memory>
#include <optional>

class QNetworkReply;

namespace pi_control::update {

inline constexpr char kUpdateUnit[] = "pi-control-update.service";

// Starts pi-control-update.service and reports how its start job ended.
class UpdateStarter : public QObject {
    Q_OBJECT

public:
    enum class Result {
        Done,      // The job ran to completion.
        Failed,    // It did not start, or ended in failure.
        Simulated, // A dry run: nothing was started.
    };
    Q_ENUM(Result)

    using QObject::QObject;
    // Emits finished() once per call, unless it never hears back.
    virtual void start() = 0;

signals:
    void finished(pi_control::update::UpdateStarter::Result result, const QString& detail);
};

// Runs `systemctl start`, which waits for the oneshot job and exits 0 only
// when it succeeds. polkit decides whether the kiosk account may start it.
class SystemctlUpdateStarter final : public UpdateStarter {
    Q_OBJECT

public:
    explicit SystemctlUpdateStarter(QString program = "systemctl", QObject* parent = nullptr);
    void start() override;

private:
    QString program_;
    QProcess process_;
};

// Pretends to install after a short delay, for --dry-run.
class DryRunUpdateStarter final : public UpdateStarter {
    Q_OBJECT

public:
    explicit DryRunUpdateStarter(std::chrono::milliseconds delay = std::chrono::seconds(3), QObject* parent = nullptr);
    void start() override;

private:
    std::chrono::milliseconds delay_;
};

struct UpdaterOptions {
    Version current;
    // Holds <base>/latest/download/pi-control-manifest.json. https:// or file://.
    // Keep in step with base= in distribution/install.sh.
    QUrl releaseBase{"https://github.com/mattsp1290/pi-control/releases"};
    // Longer than the unit's TimeoutStartSec: a job that has not ended by now
    // will not be reported.
    std::chrono::milliseconds installTimeout = std::chrono::minutes(21);
};

// Finds out whether a newer release is published, and installs it through the
// starter. The packaged unit runs the release installer as root; on success
// the package restarts pi-control, replacing this process.
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

    Updater(UpdaterOptions options, std::unique_ptr<UpdateStarter> starter, QObject* parent = nullptr);

    State state() const { return state_; }
    Version current() const { return options_.current; }
    // The newer release on offer: found by the last successful check, kept
    // through a failed re-check, and withdrawn once the updater found nothing
    // to install. A later check offers it again.
    std::optional<Version> offer() const { return offer_; }
    bool busy() const { return state_ == State::Checking || state_ == State::Installing; }
    bool canInstall() const { return offer_ && !busy(); }

    // Checks after first, then every interval. A check due while busy is skipped.
    void startPeriodicChecks(std::chrono::milliseconds first, std::chrono::milliseconds interval);
    void check();
    void install();

signals:
    void stateChanged(pi_control::update::Updater::State state);

private:
    void setState(State state);
    void finishCheck(QNetworkReply* reply);
    void installFinished(UpdateStarter::Result result, const QString& detail);

    UpdaterOptions options_;
    std::unique_ptr<UpdateStarter> starter_;
    QUrl manifestUrl_; // Empty when releaseBase is unusable.
    State state_ = State::Idle;
    std::optional<Version> offer_;
    QNetworkAccessManager network_;
    bool checkTooLarge_ = false;
    QTimer periodic_;
    std::chrono::milliseconds interval_{0};
    QTimer installWatchdog_;
};

} // namespace pi_control::update
