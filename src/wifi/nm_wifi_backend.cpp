#include "wifi/nm_wifi_backend.h"

#include "wifi/nm_messages.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>

#include <fmt/base.h>

#include <time.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace pi_control::wifi {

namespace {

using State = WifiStatus::State;
using Kind = ConnectAction::Kind;

constexpr quint32 kDeviceTypeWifi = 2;

QVariant unwrap(const QVariant& value) {
    if (value.canConvert<QDBusVariant>()) return value.value<QDBusVariant>().variant();
    return value;
}

QString objectPath(const QVariant& value) { return unwrap(value).value<QDBusObjectPath>().path(); }

QVariantMap propertyMap(const QVariant& value) { return qdbus_cast<QVariantMap>(unwrap(value)); }

QList<QDBusObjectPath> objectPaths(const QVariant& value) {
    return qdbus_cast<QList<QDBusObjectPath>>(unwrap(value));
}

bool isSet(const QString& path) { return !path.isEmpty() && path != QStringLiteral("/"); }

// Wall-clock time of a CLOCK_BOOTTIME timestamp in milliseconds.
std::optional<QDateTime> bootTimeToWallClock(qint64 bootMs) {
    if (bootMs < 0) return std::nullopt;
    timespec now{};
    if (clock_gettime(CLOCK_BOOTTIME, &now) != 0) return std::nullopt;
    const qint64 nowMs = qint64(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
    return QDateTime::currentDateTime().addMSecs(bootMs - nowMs);
}

} // namespace

NmWifiBackend::NmWifiBackend(QDBusConnection bus, QObject* parent)
    : WifiBackend(parent), bus_(std::move(bus)) {
    nm::registerTypes();
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    scanQuietTimer_.setSingleShot(true);
    connect(&scanQuietTimer_, &QTimer::timeout, this, &NmWifiBackend::finishScan);
    attemptTimer_.setSingleShot(true);
    connect(&attemptTimer_, &QTimer::timeout, this, [this] {
        if (attempt_) apply(attempt_->timerFired());
    });
    watcher_.setConnection(bus_);
    watcher_.addWatchedService(nm::kService);
    watcher_.setWatchMode(QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration);
    connect(&watcher_, &QDBusServiceWatcher::serviceRegistered, this, &NmWifiBackend::initialize);
    connect(&watcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        becomeUnavailable("NetworkManager not running");
    });
    initialize();
}

NmWifiBackend::~NmWifiBackend() {
    if (attempt_) {
        // Best effort: the profile is in memory only, so a lost delete leaves
        // nothing on disk.
        for (const auto& action : attempt_->cancelled()) {
            if (action.kind == Kind::DeleteProfile) bus_.send(nm::buildDeleteMessage(action.path));
        }
    }
    unsubscribeAll();
}

void NmWifiBackend::call(const QDBusMessage& message, ReplyHandler onReply, ErrorHandler onError) {
    auto* watcher = new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
    const quint64 generation = generation_;
    const QString method = message.member();
    const auto finished = [this, generation, method, onReply = std::move(onReply),
                           onError = std::move(onError)](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        if (generation != generation_) return;
        const QDBusMessage reply = call->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            fmt::print(stderr, "Wi‑Fi: {} failed: {}: {}\n", method.toStdString(), reply.errorName().toStdString(),
                       reply.errorMessage().toStdString());
            if (onError) onError(reply.errorName());
            return;
        }
        if (onReply) onReply(reply);
    };
    if (watcher->isFinished()) {
        QTimer::singleShot(0, this, [watcher, finished] { finished(watcher); });
    } else {
        connect(watcher, &QDBusPendingCallWatcher::finished, this, finished);
    }
}

void NmWifiBackend::subscribe(const QString& path, const char* interface, const char* name, const char* slot) {
    bus_.connect(nm::kService, path, interface, name, this, slot);
    subscriptions_.push_back({path, interface, name, slot});
}

void NmWifiBackend::unsubscribe(const QString& path, const char* name) {
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        if (it->path == path && it->name == QLatin1String(name)) {
            bus_.disconnect(nm::kService, it->path, it->interface, it->name, this, it->slot);
            it = subscriptions_.erase(it);
        } else {
            ++it;
        }
    }
}

void NmWifiBackend::unsubscribeAll() {
    for (const auto& subscription : subscriptions_) {
        bus_.disconnect(nm::kService, subscription.path, subscription.interface, subscription.name, this,
                        subscription.slot);
    }
    subscriptions_.clear();
}

void NmWifiBackend::initialize() {
    ++generation_;
    unsubscribeAll();
    auto hasOwner = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus", "NameHasOwner");
    hasOwner << QString::fromLatin1(nm::kService);
    call(
        hasOwner,
        [this](const QDBusMessage& reply) {
            if (!reply.arguments().value(0).toBool()) {
                becomeUnavailable("NetworkManager not running");
                return;
            }
            subscribe(nm::kPath, nm::kProperties, "PropertiesChanged",
                      SLOT(onManagerPropertiesChanged(QString, QVariantMap, QStringList)));
            subscribe(nm::kPath, nm::kManager, "DeviceAdded", SLOT(onDeviceAdded(QDBusObjectPath)));
            subscribe(nm::kPath, nm::kManager, "DeviceRemoved", SLOT(onDeviceRemoved(QDBusObjectPath)));
            subscribe(nm::kSettingsPath, nm::kSettings, "NewConnection", SLOT(onConnectionAdded(QDBusObjectPath)));
            subscribe(nm::kSettingsPath, nm::kSettings, "ConnectionRemoved",
                      SLOT(onConnectionRemoved(QDBusObjectPath)));
            call(nm::buildGetPropertyMessage(nm::kPath, nm::kManager, "WirelessEnabled"),
                 [this](const QDBusMessage& enabled) {
                     wirelessEnabled_ = unwrap(enabled.arguments().value(0)).toBool();
                     emit statusChanged();
                 });
            call(nm::buildCallMessage(nm::kPath, nm::kManager, "GetDevices"), [this](const QDBusMessage& devices) {
                findWifiDevice(objectPaths(devices.arguments().value(0)));
            });
            call(nm::buildCallMessage(nm::kSettingsPath, nm::kSettings, "ListConnections"),
                 [this](const QDBusMessage& list) {
                     for (const auto& path : objectPaths(list.arguments().value(0))) loadProfile(path.path());
                 });
        },
        [this](const QString&) { becomeUnavailable("System bus unavailable"); });
}

void NmWifiBackend::findWifiDevice(QList<QDBusObjectPath> devices) {
    if (!devicePath_.isEmpty()) return;
    if (devices.isEmpty()) {
        // Keep the manager subscriptions: DeviceAdded brings a late adapter in.
        available_ = false;
        unavailableDetail_ = "No Wi‑Fi adapter";
        emit statusChanged();
        return;
    }
    const QString path = devices.takeFirst().path();
    call(nm::buildGetPropertyMessage(path, nm::kDevice, "DeviceType"),
         [this, path, devices](const QDBusMessage& reply) {
             if (unwrap(reply.arguments().value(0)).toUInt() == kDeviceTypeWifi) useDevice(path);
             else findWifiDevice(devices);
         },
         [this, devices](const QString&) { findWifiDevice(devices); });
}

void NmWifiBackend::useDevice(const QString& path) {
    devicePath_ = path;
    subscribe(path, nm::kProperties, "PropertiesChanged",
              SLOT(onDevicePropertiesChanged(QString, QVariantMap, QStringList)));
    subscribe(path, nm::kDevice, "StateChanged", SLOT(onDeviceStateChanged(uint, uint, uint)));
    subscribe(path, nm::kWireless, "AccessPointAdded", SLOT(onAccessPointAdded(QDBusObjectPath)));
    subscribe(path, nm::kWireless, "AccessPointRemoved", SLOT(onAccessPointRemoved(QDBusObjectPath)));
    call(nm::buildGetAllMessage(path, nm::kDevice), [this](const QDBusMessage& reply) {
        available_ = true;
        unavailableDetail_.clear();
        applyDeviceProperties(propertyMap(reply.arguments().value(0)));
        emit statusChanged();
    });
    call(nm::buildGetAllMessage(path, nm::kWireless), [this](const QDBusMessage& reply) {
        const QVariantMap properties = propertyMap(reply.arguments().value(0));
        applyDeviceProperties(properties);
        lastScanTime_ = bootTimeToWallClock(lastScan_);
        reloadAccessPoints([this] { emit networksChanged(); });
    });
}

void NmWifiBackend::clearDevice() {
    devicePath_.clear();
    deviceState_ = 0;
    activeAccessPointPath_.clear();
    activeConnectionPath_.clear();
    activeProfilePath_.clear();
    accessPoints_.clear();
}

void NmWifiBackend::becomeUnavailable(const QString& detail) {
    ++generation_; // Replies still in flight are dropped.
    unsubscribeAll();
    // Clear first, so a listener reacting to the signals below cannot start
    // work against the vanished service.
    available_ = false;
    unavailableDetail_ = detail;
    clearDevice();
    profiles_.clear();
    supersededProfiles_.clear();
    fmt::print(stderr, "Wi‑Fi: unavailable: {}\n", detail.toStdString());
    if (attempt_) apply(attempt_->serviceVanished());
    if (scanning_) {
        scanning_ = false;
        scanQuietTimer_.stop();
        emit scanFinished(WifiError::Unavailable);
    }
    for (const QByteArray& ssid : std::exchange(pendingForgets_, {})) emit forgetFinished(ssid, WifiError::Unavailable);
    emit statusChanged();
    emit networksChanged();
}

void NmWifiBackend::onDeviceAdded(const QDBusObjectPath& path) {
    if (devicePath_.isEmpty()) findWifiDevice({path});
}

void NmWifiBackend::onDeviceRemoved(const QDBusObjectPath& path) {
    if (path.path() != devicePath_) return;
    becomeUnavailable("No Wi‑Fi adapter");
    initialize(); // Watch for the adapter to come back.
}

void NmWifiBackend::applyDeviceProperties(const QVariantMap& properties) {
    if (properties.contains("State")) deviceState_ = unwrap(properties["State"]).toUInt();
    if (properties.contains("ActiveAccessPoint")) setActiveAccessPoint(objectPath(properties["ActiveAccessPoint"]));
    if (properties.contains("ActiveConnection")) setActiveConnection(objectPath(properties["ActiveConnection"]));
    if (properties.contains("LastScan")) {
        const qint64 lastScan = unwrap(properties["LastScan"]).toLongLong();
        const bool changed = lastScan != lastScan_;
        lastScan_ = lastScan;
        if (changed && scanning_ && scanQuietTimer_.isActive()) finishScan();
        // A change before the RequestScan reply is handled by the reply.
    }
}

void NmWifiBackend::setActiveAccessPoint(const QString& path) {
    const QString next = isSet(path) ? path : QString();
    if (next == activeAccessPointPath_) return;
    if (!activeAccessPointPath_.isEmpty()) unsubscribe(activeAccessPointPath_, "PropertiesChanged");
    activeAccessPointPath_ = next;
    if (next.isEmpty()) return;
    subscribe(next, nm::kProperties, "PropertiesChanged",
              SLOT(onActiveAccessPointPropertiesChanged(QString, QVariantMap, QStringList)));
    loadAccessPoint(next, [this] {
        emit statusChanged();
        emit networksChanged();
    });
}

void NmWifiBackend::setActiveConnection(const QString& path) {
    activeConnectionPath_ = isSet(path) ? path : QString();
    activeProfilePath_.clear();
    if (activeConnectionPath_.isEmpty()) return;
    const QString active = activeConnectionPath_;
    call(nm::buildGetPropertyMessage(active, nm::kActive, "Connection"), [this, active](const QDBusMessage& reply) {
        if (active == activeConnectionPath_) activeProfilePath_ = objectPath(reply.arguments().value(0));
    });
}

void NmWifiBackend::loadAccessPoint(const QString& path, std::function<void()> done) {
    call(
        nm::buildGetAllMessage(path, nm::kAccessPoint),
        [this, path, done](const QDBusMessage& reply) {
            const QVariantMap properties = propertyMap(reply.arguments().value(0));
            AccessPointInfo info;
            info.path = path;
            info.ssidBytes = unwrap(properties.value("Ssid")).toByteArray();
            info.strength = int(unwrap(properties.value("Strength")).toUInt());
            info.flags = unwrap(properties.value("Flags")).toUInt();
            info.wpaFlags = unwrap(properties.value("WpaFlags")).toUInt();
            info.rsnFlags = unwrap(properties.value("RsnFlags")).toUInt();
            accessPoints_[path] = info;
            if (done) done();
        },
        [done](const QString&) {
            if (done) done();
        });
}

void NmWifiBackend::reloadAccessPoints(std::function<void()> done) {
    call(
        nm::buildCallMessage(devicePath_, nm::kWireless, "GetAllAccessPoints"),
        [this, done](const QDBusMessage& reply) {
            const QList<QDBusObjectPath> paths = objectPaths(reply.arguments().value(0));
            std::map<QString, AccessPointInfo> kept;
            for (const auto& path : paths) {
                if (auto it = accessPoints_.find(path.path()); it != accessPoints_.end()) kept.insert(*it);
            }
            accessPoints_ = std::move(kept);
            if (paths.isEmpty()) {
                done();
                return;
            }
            auto remaining = std::make_shared<qsizetype>(paths.size());
            for (const auto& path : paths) {
                loadAccessPoint(path.path(), [remaining, done] {
                    if (--*remaining == 0) done();
                });
            }
        },
        [done](const QString&) { done(); });
}

void NmWifiBackend::loadProfile(const QString& path) {
    call(nm::buildCallMessage(path, nm::kConnection, "GetSettings"), [this, path](const QDBusMessage& reply) {
        const auto settings = qdbus_cast<nm::Settings>(reply.arguments().value(0));
        if (settings.value("connection").value("type").toString() != QLatin1String("802-11-wireless")) return;
        const QByteArray ssid = unwrap(settings.value("802-11-wireless").value("ssid")).toByteArray();
        if (ssid.isEmpty()) return;
        profiles_[path] = ssid;
        emit networksChanged();
    });
}

void NmWifiBackend::onManagerPropertiesChanged(const QString& interface, const QVariantMap& changed,
                                               const QStringList&) {
    if (interface != QLatin1String(nm::kManager) || !changed.contains("WirelessEnabled")) return;
    wirelessEnabled_ = unwrap(changed["WirelessEnabled"]).toBool();
    emit statusChanged();
}

void NmWifiBackend::onDevicePropertiesChanged(const QString&, const QVariantMap& changed, const QStringList&) {
    applyDeviceProperties(changed);
    emit statusChanged();
}

void NmWifiBackend::onActiveAccessPointPropertiesChanged(const QString& interface, const QVariantMap& changed,
                                                         const QStringList&) {
    if (interface != QLatin1String(nm::kAccessPoint) || !changed.contains("Strength")) return;
    auto it = accessPoints_.find(activeAccessPointPath_);
    if (it == accessPoints_.end()) return;
    it->second.strength = int(unwrap(changed["Strength"]).toUInt());
    emit statusChanged();
}

void NmWifiBackend::onDeviceStateChanged(uint newState, uint oldState, uint reason) {
    deviceState_ = newState;
    if (attempt_) apply(attempt_->deviceStateChanged(newState, oldState, reason));
    emit statusChanged();
}

void NmWifiBackend::onActiveStateChanged(uint state, uint reason) {
    if (attempt_) apply(attempt_->activeStateChanged(state, reason));
}

void NmWifiBackend::onAccessPointAdded(const QDBusObjectPath& path) {
    loadAccessPoint(path.path(), [this] { emit networksChanged(); });
}

void NmWifiBackend::onAccessPointRemoved(const QDBusObjectPath& path) {
    if (accessPoints_.erase(path.path()) > 0) emit networksChanged();
}

void NmWifiBackend::onConnectionAdded(const QDBusObjectPath& path) { loadProfile(path.path()); }

void NmWifiBackend::onConnectionRemoved(const QDBusObjectPath& path) {
    if (profiles_.erase(path.path()) > 0) emit networksChanged();
}

WifiStatus NmWifiBackend::status() const {
    if (!available_) return {.state = State::Unavailable, .detail = unavailableDetail_};
    if (!wirelessEnabled_) return {.state = State::RadioOff};
    const auto active = accessPoints_.find(activeAccessPointPath_);
    const bool haveActive = active != accessPoints_.end();
    if (deviceState_ == device_state::kActivated) {
        WifiStatus status{.state = State::Connected};
        if (haveActive) {
            status.ssid = displaySsid(active->second.ssidBytes);
            status.strength = active->second.strength;
        }
        return status;
    }
    if (deviceState_ >= device_state::kPrepare && deviceState_ <= device_state::kSecondaries) {
        // After the attempt's result, the device is rejoining another network.
        const bool joining = attempt_ && !attempt_->result();
        const QByteArray ssid = joining ? attemptSsid_ : haveActive ? active->second.ssidBytes : QByteArray();
        return {.state = State::Connecting, .ssid = displaySsid(ssid)};
    }
    return {.state = State::Disconnected};
}

std::vector<AccessPointInfo> NmWifiBackend::accessPoints() const {
    std::vector<AccessPointInfo> list;
    list.reserve(accessPoints_.size());
    for (const auto& [path, info] : accessPoints_) list.push_back(info);
    return list;
}

QByteArray NmWifiBackend::activeSsid() const {
    if (deviceState_ != device_state::kActivated) return {};
    const auto active = accessPoints_.find(activeAccessPointPath_);
    return active == accessPoints_.end() ? QByteArray() : active->second.ssidBytes;
}

std::vector<WifiNetwork> NmWifiBackend::networks() const {
    std::vector<QByteArray> saved;
    for (const auto& [path, ssid] : profiles_) saved.push_back(ssid);
    return mergeAccessPoints(accessPoints(), activeSsid(), saved);
}

std::vector<QString> NmWifiBackend::profilesFor(const QByteArray& ssidBytes) const {
    std::vector<QString> paths;
    for (const auto& [path, ssid] : profiles_) {
        if (ssid == ssidBytes) paths.push_back(path);
    }
    return paths;
}

void NmWifiBackend::requestScan() {
    if (scanning_) return;
    if (!available_) {
        QTimer::singleShot(0, this, [this] { emit scanFinished(WifiError::Unavailable); });
        return;
    }
    scanning_ = true;
    scanBaseline_ = lastScan_;
    call(
        nm::buildRequestScanMessage(devicePath_),
        [this](const QDBusMessage&) {
            if (lastScan_ != scanBaseline_) finishScan(); // Already done.
            else scanQuietTimer_.start(kScanQuietMs);
        },
        [this](const QString& errorName) {
            scanning_ = false;
            const WifiError error = classifyCallError(errorName);
            emit scanFinished(error == WifiError::NotPermitted ? error : WifiError::Failed);
        });
}

void NmWifiBackend::finishScan() {
    if (!scanning_) return;
    scanQuietTimer_.stop();
    reloadAccessPoints([this] {
        scanning_ = false;
        lastScanTime_ = QDateTime::currentDateTime();
        emit networksChanged();
        emit scanFinished(WifiError::None);
    });
}

bool NmWifiBackend::connectTo(const WifiNetwork& network, const QString& password) {
    if (attempt_) return false;
    attemptSsid_ = network.ssidBytes;
    if (!available_) {
        startAttempt(false);
        QTimer::singleShot(0, this, [this] {
            if (attempt_) apply(attempt_->callFailed(ConnectAttempt::Call::Activate,
                                                     "org.freedesktop.DBus.Error.ServiceUnknown"));
        });
        emit statusChanged();
        return true;
    }
    const std::vector<QString> saved = profilesFor(network.ssidBytes);
    if (!saved.empty() && password.isEmpty()) {
        startAttempt(false);
        const QString ap = strongestAccessPoint(accessPoints(), network.ssidBytes);
        call(
            nm::buildActivateMessage(saved.front(), devicePath_, ap),
            [this](const QDBusMessage& reply) {
                if (attempt_) apply(attempt_->activateReplied(objectPath(reply.arguments().value(0))));
            },
            [this](const QString& errorName) {
                if (attempt_) apply(attempt_->callFailed(ConnectAttempt::Call::Activate, errorName));
            });
    } else if (!saved.empty()) {
        // A new password gets a new profile rather than an edit: re-marshalling
        // NetworkManager's typed settings loses D-Bus types. The old profiles
        // go only once the new one has joined, so a mistyped retry loses
        // nothing.
        supersededProfiles_ = saved;
        addAndActivate(network, password);
    } else {
        addAndActivate(network, password);
    }
    emit statusChanged();
    return true;
}

void NmWifiBackend::startAttempt(bool createdProfile) {
    const QString previous = deviceState_ == device_state::kActivated ? activeProfilePath_ : QString();
    attempt_.emplace(createdProfile, previous, deviceState_);
}

void NmWifiBackend::addAndActivate(const WifiNetwork& network, const QString& password) {
    if (!attempt_) startAttempt(true);
    const QString ap = strongestAccessPoint(accessPoints(), network.ssidBytes);
    call(
        nm::buildAddAndActivateMessage(network.ssidBytes, network.security, password, devicePath_, ap),
        [this](const QDBusMessage& reply) {
            if (!attempt_) return;
            apply(attempt_->addReplied(objectPath(reply.arguments().value(0)),
                                       objectPath(reply.arguments().value(1))));
        },
        [this](const QString& errorName) {
            if (attempt_) apply(attempt_->callFailed(ConnectAttempt::Call::AddAndActivate, errorName));
        });
}

void NmWifiBackend::adoptAttemptForTesting(ConnectAttempt attempt, const QByteArray& ssidBytes) {
    attempt_.emplace(std::move(attempt));
    attemptSsid_ = ssidBytes;
}

void NmWifiBackend::apply(const ConnectActions& actions) {
    for (const auto& action : actions) {
        switch (action.kind) {
        case Kind::SubscribeActive:
            if (!subscribedActivePath_.isEmpty()) unsubscribe(subscribedActivePath_, "StateChanged");
            subscribedActivePath_ = action.path;
            subscribe(action.path, nm::kActive, "StateChanged", SLOT(onActiveStateChanged(uint, uint)));
            break;
        case Kind::ReadActiveState:
            call(
                nm::buildGetPropertyMessage(action.path, nm::kActive, "State"),
                [this](const QDBusMessage& reply) {
                    if (attempt_) apply(attempt_->activeStateRead(unwrap(reply.arguments().value(0)).toUInt()));
                },
                [this](const QString& errorName) {
                    if (attempt_) apply(attempt_->callFailed(ConnectAttempt::Call::ReadActiveState, errorName));
                });
            break;
        case Kind::SaveProfile:
            call(
                nm::buildSaveMessage(action.path),
                [this](const QDBusMessage&) {
                    if (attempt_) apply(attempt_->saveReplied());
                },
                [this](const QString& errorName) {
                    if (attempt_) apply(attempt_->callFailed(ConnectAttempt::Call::Save, errorName));
                });
            break;
        case Kind::DeleteProfile:
            call(nm::buildDeleteMessage(action.path), {});
            break;
        case Kind::Deactivate:
            if (isSet(action.path)) call(nm::buildDeactivateMessage(action.path), {});
            break;
        case Kind::ActivatePrevious:
            fmt::print(stderr, "Wi‑Fi: returning to the previous network\n");
            call(nm::buildActivateMessage(action.path, devicePath_, "/"), {});
            break;
        case Kind::StartTimer:
            attemptTimer_.start(action.milliseconds);
            break;
        case Kind::Finish:
            fmt::print(stderr, "Wi‑Fi: connect {} → {}\n", displaySsid(attemptSsid_).toStdString(),
                       toString(action.error));
            if (action.error == WifiError::None) {
                for (const QString& old : supersededProfiles_) call(nm::buildDeleteMessage(old), {});
            }
            supersededProfiles_.clear();
            emit networksChanged();
            emit connectFinished(attemptSsid_, action.error);
            break;
        case Kind::RecoveryDone:
            attemptTimer_.stop();
            if (!subscribedActivePath_.isEmpty()) unsubscribe(subscribedActivePath_, "StateChanged");
            subscribedActivePath_.clear();
            attempt_.reset();
            attemptSsid_.clear();
            emit statusChanged();
            break;
        }
    }
}

void NmWifiBackend::forget(const QByteArray& ssidBytes) {
    const std::vector<QString> paths = profilesFor(ssidBytes);
    if (!available_ || paths.empty()) {
        const WifiError error = available_ ? WifiError::None : WifiError::Unavailable;
        QTimer::singleShot(0, this, [this, ssidBytes, error] { emit forgetFinished(ssidBytes, error); });
        return;
    }
    pendingForgets_.push_back(ssidBytes);
    struct Progress {
        qsizetype remaining;
        WifiError error = WifiError::None;
    };
    auto progress = std::make_shared<Progress>(Progress{qsizetype(paths.size())});
    const auto done = [this, ssidBytes, progress] {
        if (--progress->remaining > 0) return;
        if (auto it = std::find(pendingForgets_.begin(), pendingForgets_.end(), ssidBytes); it != pendingForgets_.end()) {
            pendingForgets_.erase(it);
        }
        fmt::print(stderr, "Wi‑Fi: forget {} → {}\n", displaySsid(ssidBytes).toStdString(),
                   toString(progress->error));
        emit networksChanged();
        emit forgetFinished(ssidBytes, progress->error);
    };
    for (const QString& path : paths) {
        call(
            nm::buildDeleteMessage(path),
            [this, path, done](const QDBusMessage&) {
                profiles_.erase(path);
                done();
            },
            [progress, done](const QString& errorName) {
                if (progress->error == WifiError::None) {
                    const WifiError error = classifyCallError(errorName);
                    progress->error = error == WifiError::NotPermitted ? error : WifiError::Failed;
                }
                done();
            });
    }
}

} // namespace pi_control::wifi
