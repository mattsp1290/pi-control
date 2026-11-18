#include "update/updater.h"

#include <QDBusConnection>
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

QDBusConnection installBus(const UpdaterOptions& options) {
    // An unregistered connection is always disconnected: tests cannot reach systemd.
    return options.failureTest ? QDBusConnection(QStringLiteral("pi-control-test-disconnected"))
                               : QDBusConnection::systemBus();
}

} // namespace

Updater::Updater(UpdaterOptions options, QObject* parent) : QObject(parent), options_(std::move(options)) {
    connect(&periodic_, &QTimer::timeout, this, [this] {
        periodic_.start(interval_);
        check();
    });
}

void Updater::startPeriodicChecks(std::chrono::milliseconds first, std::chrono::milliseconds interval) {
    interval_ = interval;
    periodic_.start(first);
}

void Updater::setState(State state) {
    if (state_ == state) return;
    state_ = state;
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
    const bool newer = *latest_ > options_.current;
    fmt::print(stderr, "Update check: latest {}, running {}\n", latest_->toString().toStdString(),
               options_.current.toString().toStdString());
    setState(newer ? State::Available : State::UpToDate);
}

void Updater::install() {
    if (state_ != State::Available && state_ != State::InstallFailed) return;
    setState(State::Installing);
    if (isDryRun()) {
        fmt::print("Dry run: {} was not started.\n", kUpdateUnit);
        emit dryRunInstallRequested();
        return;
    }
    QDBusConnection bus = installBus(options_);
    // Watch for the start job before creating it. systemd only sends
    // JobRemoved once some client has subscribed.
    if (!subscribed_ && bus.isConnected()) {
        subscribed_ = bus.connect(kSystemd, kSystemdPath, kManager, "JobRemoved", this,
                                  SLOT(onJobRemoved(uint, QDBusObjectPath, QString, QString)));
        bus.asyncCall(QDBusMessage::createMethodCall(kSystemd, kSystemdPath, kManager, "Subscribe"));
    }
    auto message = QDBusMessage::createMethodCall(kSystemd, kSystemdPath, kManager, "StartUnit");
    message << QString(kUpdateUnit) << QStringLiteral("replace");
    fmt::print(stderr, "Update: starting {} for {}\n", kUpdateUnit, latest_ ? latest_->toString().toStdString() : "?");
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 25000), this);
    const auto finished = [this](QDBusPendingCallWatcher* call) {
        const QDBusPendingReply<QDBusObjectPath> reply = *call;
        if (reply.isError()) installFailed(reply.error().message());
        call->deleteLater();
    };
    if (watcher->isFinished()) {
        QTimer::singleShot(0, this, [=] { finished(watcher); });
    } else {
        connect(watcher, &QDBusPendingCallWatcher::finished, this, finished);
    }
}

void Updater::onJobRemoved(uint, const QDBusObjectPath&, const QString& unit, const QString& result) {
    if (unit != kUpdateUnit || state_ != State::Installing) return;
    if (result != "done") {
        installFailed(QString("%1 finished with result %2").arg(kUpdateUnit, result));
        return;
    }
    // Success normally restarts this process before the job ends. Still
    // running means the installer found nothing newer: ask again.
    fmt::print(stderr, "Update: {} finished without replacing pi-control\n", kUpdateUnit);
    setState(State::Idle);
    check();
}

void Updater::installFailed(const QString& detail) {
    if (state_ != State::Installing) return;
    fmt::print(stderr, "Update failed: {}\n", detail.toStdString());
    setState(State::InstallFailed);
}

} // namespace pi_control::update
