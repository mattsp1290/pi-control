#include "update/updater.h"

#include <QNetworkReply>
#include <QNetworkRequest>

#include <fmt/base.h>

namespace pi_control::update {

namespace {

constexpr qint64 kManifestLimit = 64 * 1024;
constexpr int kManifestTimeoutMs = 20000;

QUrl manifestUrl(const QUrl& base) {
    if (base.scheme() != "https" && base.scheme() != "file") return {};
    QUrl url = base;
    QString path = url.path();
    while (path.endsWith('/')) path.chop(1);
    url.setPath(path + "/latest/download/pi-control-manifest.json");
    return url;
}

} // namespace

SystemctlUpdateStarter::SystemctlUpdateStarter(QString program, QObject* parent)
    : UpdateStarter(parent), program_(std::move(program)) {
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        emit finished(ok ? Result::Done : Result::Failed, QString::fromUtf8(process_.readAll()).trimmed());
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) emit finished(Result::Failed, process_.errorString());
    });
}

void SystemctlUpdateStarter::start() {
    if (process_.state() != QProcess::NotRunning) return;
    process_.setProcessChannelMode(QProcess::MergedChannels);
    process_.setStandardInputFile(QProcess::nullDevice());
    // No password agent in a kiosk: polkit's rule alone decides.
    process_.start(program_, {"--no-ask-password", "start", kUpdateUnit});
}

DryRunUpdateStarter::DryRunUpdateStarter(std::chrono::milliseconds delay, QObject* parent)
    : UpdateStarter(parent), delay_(delay) {}

void DryRunUpdateStarter::start() {
    fmt::print("Dry run: {} was not started.\n", kUpdateUnit);
    QTimer::singleShot(delay_, this, [this] { emit finished(Result::Simulated, {}); });
}

Updater::Updater(UpdaterOptions options, std::unique_ptr<UpdateStarter> starter, QObject* parent)
    : QObject(parent), options_(std::move(options)), starter_(std::move(starter)),
      manifestUrl_(manifestUrl(options_.releaseBase)) {
    Q_ASSERT(starter_);
    connect(starter_.get(), &UpdateStarter::finished, this, &Updater::installFinished);
    connect(&periodic_, &QTimer::timeout, this, [this] {
        periodic_.start(interval_);
        check();
    });
    installWatchdog_.setSingleShot(true);
    connect(&installWatchdog_, &QTimer::timeout, this, [this] {
        installFinished(UpdateStarter::Result::Failed, QString("no result from %1").arg(kUpdateUnit));
    });
}

void Updater::startPeriodicChecks(std::chrono::milliseconds first, std::chrono::milliseconds interval) {
    interval_ = interval;
    periodic_.start(first);
}

void Updater::setState(State state) {
    if (state_ == state) return;
    state_ = state;
    if (state != State::Installing) installWatchdog_.stop();
    emit stateChanged(state);
}

void Updater::check() {
    if (busy()) return;
    if (manifestUrl_.isEmpty()) {
        fmt::print(stderr, "Update check failed: release base must be https:// or file://\n");
        setState(State::CheckFailed);
        return;
    }
    QNetworkRequest request(manifestUrl_);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kManifestTimeoutMs);
    checkTooLarge_ = false;
    setState(State::Checking);
    QNetworkReply* reply = network_.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64) {
        if (received > kManifestLimit && !checkTooLarge_) {
            checkTooLarge_ = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishCheck(reply); });
}

void Updater::finishCheck(QNetworkReply* reply) {
    reply->deleteLater();
    const auto fail = [this](const QString& why) {
        fmt::print(stderr, "Update check failed: {}\n", why.toStdString());
        setState(State::CheckFailed);
    };
    if (checkTooLarge_) return fail("release manifest too large");
    if (reply->error() != QNetworkReply::NoError) return fail(reply->errorString());
    const QByteArray body = reply->readAll();
    if (body.size() > kManifestLimit) return fail("release manifest too large");
    const auto manifest = parseManifest(body);
    if (const auto* error = std::get_if<QString>(&manifest)) return fail(*error);
    const Version latest = std::get<Version>(manifest);
    fmt::print(stderr, "Update check: latest {}, running {}\n", latest.toString().toStdString(),
               options_.current.toString().toStdString());
    if (latest > options_.current) {
        offer_ = latest;
        setState(State::Available);
    } else {
        offer_.reset();
        setState(State::UpToDate);
    }
}

void Updater::install() {
    if (!canInstall()) return;
    fmt::print(stderr, "Update: starting {} for {} or newer\n", kUpdateUnit, offer_->toString().toStdString());
    setState(State::Installing);
    installWatchdog_.start(options_.installTimeout);
    starter_->start();
}

void Updater::installFinished(UpdateStarter::Result result, const QString& detail) {
    if (state_ != State::Installing) return;
    switch (result) {
    case UpdateStarter::Result::Simulated:
        setState(State::Available);
        return;
    case UpdateStarter::Result::Failed:
        fmt::print(stderr, "Update failed: {}\n", detail.toStdString());
        setState(State::InstallFailed);
        return;
    case UpdateStarter::Result::Done:
        // Success restarts this process before the job ends. Still running
        // means the installer found nothing newer than the installed package.
        fmt::print(stderr, "Update: {} finished without replacing pi-control\n", kUpdateUnit);
        offer_.reset();
        setState(State::NothingInstalled);
        return;
    }
}

} // namespace pi_control::update
