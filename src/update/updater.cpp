#include "update/updater.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <fmt/base.h>

namespace pi_control::update {

namespace {

constexpr qint64 kManifestLimit = 64 * 1024;
constexpr int kManifestTimeoutMs = 20000;
constexpr char kSystemd[] = "org.freedesktop.systemd1";
constexpr char kSystemdPath[] = "/org/freedesktop/systemd1";
constexpr char kManager[] = "org.freedesktop.systemd1.Manager";

QDBusMessage managerCall(const char* method) {
    return QDBusMessage::createMethodCall(kSystemd, kSystemdPath, kManager, method);
}

} // namespace

SystemdUpdateStarter::SystemdUpdateStarter(QDBusConnection bus, QObject* parent)
    : UpdateStarter(parent), bus_(std::move(bus)) {}

void SystemdUpdateStarter::start() {
    if (running_) return;
    running_ = true;
    job_.clear();
    early_.reset();
    // Watch for the start job before creating it: without the JobRemoved
    // signal the result can never be observed, so do not start at all.
    if (!bus_.connect(kSystemd, kSystemdPath, kManager, "JobRemoved", this,
                      SLOT(onJobRemoved(uint, QDBusObjectPath, QString, QString)))) {
        const QString reason = bus_.lastError().isValid() ? bus_.lastError().message() : "system bus unavailable";
        QTimer::singleShot(0, this, [this, reason] { finish(false, "cannot watch systemd jobs: " + reason); });
        return;
    }
    // systemd sends JobRemoved only while some client is subscribed.
    auto* subscribed = new QDBusPendingCallWatcher(bus_.asyncCall(managerCall("Subscribe")), this);
    connect(subscribed, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
        const QDBusPendingReply<> reply = *call;
        call->deleteLater();
        if (reply.isError()) finish(false, "cannot subscribe to systemd: " + reply.error().message());
    });
    auto message = managerCall("StartUnit");
    message << QString(kUpdateUnit) << QStringLiteral("replace");
    auto* started = new QDBusPendingCallWatcher(bus_.asyncCall(message, 25000), this);
    connect(started, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* call) {
        const QDBusPendingReply<QDBusObjectPath> reply = *call;
        call->deleteLater();
        if (!running_) return;
        if (reply.isError()) {
            finish(false, reply.error().message());
            return;
        }
        job_ = reply.value().path();
        if (early_ && early_->first == job_) onJobRemoved(0, QDBusObjectPath(early_->first), kUpdateUnit, early_->second);
    });
}

void SystemdUpdateStarter::onJobRemoved(uint, const QDBusObjectPath& job, const QString& unit, const QString& result) {
    if (!running_ || unit != kUpdateUnit) return;
    if (job_.isEmpty()) {
        early_.emplace(job.path(), result);
        return;
    }
    if (job.path() != job_) return; // Someone else's run, such as a manual systemctl start.
    finish(result == "done", QString("%1 finished with result %2").arg(kUpdateUnit, result));
}

void SystemdUpdateStarter::finish(bool ok, const QString& detail) {
    if (!running_) return;
    running_ = false;
    bus_.disconnect(kSystemd, kSystemdPath, kManager, "JobRemoved", this,
                    SLOT(onJobRemoved(uint, QDBusObjectPath, QString, QString)));
    if (bus_.isConnected()) bus_.asyncCall(managerCall("Unsubscribe"));
    emit finished(ok, detail);
}

Updater::Updater(UpdaterOptions options, std::unique_ptr<UpdateStarter> starter, QObject* parent)
    : QObject(parent), options_(std::move(options)), starter_(std::move(starter)) {
    Q_ASSERT(starter_ || options_.dryRun);
    if (starter_) connect(starter_.get(), &UpdateStarter::finished, this, &Updater::installFinished);
    connect(&periodic_, &QTimer::timeout, this, [this] {
        periodic_.start(interval_);
        check();
    });
    installTimer_.setSingleShot(true);
    connect(&installTimer_, &QTimer::timeout, this, [this] {
        if (options_.dryRun) {
            setState(State::Available);
        } else {
            installFinished(false, QString("no result from %1").arg(kUpdateUnit));
        }
    });
}

void Updater::startPeriodicChecks(std::chrono::milliseconds first, std::chrono::milliseconds interval) {
    interval_ = interval;
    periodic_.start(first);
}

void Updater::setState(State state) {
    if (state_ == state) return;
    state_ = state;
    if (state != State::Installing) installTimer_.stop();
    emit stateChanged(state);
}

void Updater::check() {
    if (state_ == State::Checking || state_ == State::Installing) return;
    const QString scheme = options_.releaseBase.scheme();
    if (scheme != "https" && scheme != "file") {
        fmt::print(stderr, "Update check: release base must be https:// or file://\n");
        setState(State::CheckFailed);
        return;
    }
    QUrl url = options_.releaseBase;
    QString path = url.path();
    while (path.endsWith('/')) path.chop(1);
    url.setPath(path + "/latest/download/pi-control-manifest.json");
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kManifestTimeoutMs);
    setState(State::Checking);
    QNetworkReply* reply = network_.get(request);
    auto tooLarge = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply, tooLarge](qint64 received, qint64) {
        if (received > kManifestLimit && !*tooLarge) {
            *tooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, tooLarge] { finishCheck(reply, *tooLarge); });
}

void Updater::finishCheck(QNetworkReply* reply, bool tooLarge) {
    reply->deleteLater();
    const QByteArray body = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray();
    QString error;
    if (tooLarge || body.size() > kManifestLimit) {
        error = "release manifest too large";
    } else if (reply->error() != QNetworkReply::NoError) {
        error = reply->errorString();
    }
    ManifestResult manifest;
    if (error.isEmpty()) {
        manifest = parseManifest(body);
        error = manifest.error;
    }
    if (!error.isEmpty()) {
        fmt::print(stderr, "Update check failed: {}\n", error.toStdString());
        setState(State::CheckFailed);
        return;
    }
    latest_ = manifest.version;
    fmt::print(stderr, "Update check: latest {}, running {}\n", latest_->toString().toStdString(),
               options_.current.toString().toStdString());
    setState(newerKnown() ? State::Available : State::UpToDate);
}

bool Updater::canInstall() const {
    // After a failed re-check the release found earlier is still worth offering.
    return newerKnown()
           && (state_ == State::Available || state_ == State::InstallFailed || state_ == State::CheckFailed);
}

void Updater::install() {
    if (!canInstall()) return;
    setState(State::Installing);
    if (options_.dryRun) {
        fmt::print("Dry run: {} was not started.\n", kUpdateUnit);
        installTimer_.start(options_.dryRunDuration);
        emit dryRunInstallRequested();
        return;
    }
    fmt::print(stderr, "Update: starting {} for {} or newer\n", kUpdateUnit, latest_->toString().toStdString());
    installTimer_.start(options_.installTimeout);
    starter_->start();
}

void Updater::installFinished(bool ok, const QString& detail) {
    if (state_ != State::Installing) return;
    if (!ok) {
        fmt::print(stderr, "Update failed: {}\n", detail.toStdString());
        setState(State::InstallFailed);
        return;
    }
    // Success restarts this process before the job ends. Still running means
    // the installer found nothing newer than the installed package.
    fmt::print(stderr, "Update: {} finished without replacing pi-control\n", kUpdateUnit);
    setState(State::NothingInstalled);
}

} // namespace pi_control::update
