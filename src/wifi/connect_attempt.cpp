#include "wifi/connect_attempt.h"

namespace pi_control::wifi {

namespace {

using Kind = ConnectAction::Kind;

void append(ConnectActions& to, ConnectActions more) {
    to.insert(to.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
}

} // namespace

WifiError classifyConnectFailure(std::optional<quint32> deviceReason, std::optional<quint32> activeReason) {
    if (deviceReason == reason::kDeviceNoSecrets || activeReason == reason::kActiveNoSecrets) {
        return WifiError::WrongPassword;
    }
    if (deviceReason == reason::kDeviceSsidNotFound) return WifiError::NotFound;
    return WifiError::Failed;
}

WifiError classifyCallError(const QString& errorName) {
    if (errorName.endsWith(".PermissionDenied") || errorName.endsWith(".NotAllowed")) {
        return WifiError::NotPermitted;
    }
    if (errorName.endsWith(".UnknownConnection") || errorName.endsWith(".UnknownDevice")
        || errorName.endsWith(".ObjectNotFound") || errorName == "org.freedesktop.DBus.Error.UnknownObject") {
        return WifiError::NotFound;
    }
    if (errorName == "org.freedesktop.DBus.Error.ServiceUnknown"
        || errorName == "org.freedesktop.DBus.Error.NameHasNoOwner") {
        return WifiError::Unavailable;
    }
    return WifiError::Failed;
}

ConnectAttempt::ConnectAttempt(bool createdProfile, QString previousProfile, quint32 deviceState)
    : createdProfile_(createdProfile), previousProfile_(std::move(previousProfile)), deviceState_(deviceState) {}

ConnectActions ConnectAttempt::addReplied(const QString& profilePath, const QString& activePath) {
    if (phase_ != Phase::Starting) return {};
    profilePath_ = profilePath;
    return replied(activePath);
}

ConnectActions ConnectAttempt::activateReplied(const QString& activePath) {
    if (phase_ != Phase::Starting) return {};
    return replied(activePath);
}

ConnectActions ConnectAttempt::replied(const QString& activePath) {
    activePath_ = activePath;
    phase_ = Phase::Waiting;
    // Read after subscribing: the state may have changed before the signal
    // subscription existed.
    return {{Kind::SubscribeActive, activePath},
            {Kind::ReadActiveState, activePath},
            {Kind::StartTimer, {}, kConnectTimeoutMs}};
}

ConnectActions ConnectAttempt::callFailed(Call call, const QString& errorName) {
    switch (call) {
    case Call::AddAndActivate:
    case Call::Activate:
        if (phase_ != Phase::Starting) return {};
        return fail(classifyCallError(errorName));
    case Call::ReadActiveState:
        // NetworkManager keeps an active connection while it is activating or
        // activated, so one that is already gone has ended.
        if (phase_ != Phase::Waiting || classifyCallError(errorName) != WifiError::NotFound) return {};
        return fail(classifyConnectFailure(failureReason_, std::nullopt));
    case Call::Save:
        // The network works for this session even if it could not be saved.
        if (phase_ != Phase::Saving) return {};
        return finish(WifiError::None);
    }
    return {};
}

ConnectActions ConnectAttempt::activeStateRead(quint32 state) {
    if (phase_ != Phase::Waiting) return {};
    if (state == active_state::kActivated) return succeed();
    if (state == active_state::kDeactivated) return fail(classifyConnectFailure(failureReason_, std::nullopt));
    return {};
}

ConnectActions ConnectAttempt::activeStateChanged(quint32 state, quint32 reason) {
    if (phase_ != Phase::Waiting) return {};
    if (state == active_state::kActivated) return succeed();
    if (state == active_state::kDeactivated) return fail(classifyConnectFailure(failureReason_, reason));
    return {};
}

ConnectActions ConnectAttempt::deviceStateChanged(quint32 newState, quint32, quint32 reason) {
    deviceState_ = newState;
    switch (phase_) {
    case Phase::Waiting:
        if ((newState == device_state::kFailed || newState == device_state::kDisconnected) && reason != 0) {
            failureReason_ = reason;
        }
        return {};
    case Phase::Recovering:
    case Phase::Rejoining:
        if (newState == device_state::kActivated) return recovered();
        // The explicit rejoin failed; nothing more to try.
        if (phase_ == Phase::Rejoining && newState == device_state::kFailed) return recovered();
        return {};
    case Phase::Starting:
    case Phase::Saving:
    case Phase::Done:
        return {};
    }
    return {};
}

ConnectActions ConnectAttempt::saveReplied() {
    if (phase_ != Phase::Saving) return {};
    return finish(WifiError::None);
}

ConnectActions ConnectAttempt::timerFired() {
    switch (phase_) {
    case Phase::Waiting: {
        ConnectActions actions{{Kind::Deactivate, activePath_}};
        append(actions, fail(WifiError::Timeout));
        return actions;
    }
    case Phase::Saving:
        return finish(WifiError::None);
    case Phase::Recovering:
        return recoveryTimedOut();
    case Phase::Rejoining:
        return recovered();
    case Phase::Starting:
    case Phase::Done:
        return {};
    }
    return {};
}

ConnectActions ConnectAttempt::recoveryTimedOut() {
    // Already back on a network, or on its way: do not interrupt it.
    if (deviceState_ >= device_state::kPrepare && deviceState_ <= device_state::kActivated) return recovered();
    phase_ = Phase::Rejoining;
    return {{Kind::ActivatePrevious, previousProfile_}, {Kind::StartTimer, {}, kRecoveryRejoinMs}};
}

ConnectActions ConnectAttempt::recovered() {
    phase_ = Phase::Done;
    return {{Kind::RecoveryDone}};
}

ConnectActions ConnectAttempt::serviceVanished() {
    if (phase_ == Phase::Done) return {};
    ConnectActions actions;
    if (!finished()) {
        result_ = WifiError::Unavailable;
        actions.push_back({Kind::Finish, {}, 0, WifiError::Unavailable});
    }
    phase_ = Phase::Done;
    actions.push_back({Kind::RecoveryDone});
    return actions;
}

ConnectActions ConnectAttempt::cancelled() {
    if (phase_ == Phase::Done) return {};
    ConnectActions actions;
    // While saving, the profile is the working connection: keep it.
    if (!finished() && phase_ != Phase::Saving && createdProfile_ && !profilePath_.isEmpty()) {
        actions.push_back({Kind::DeleteProfile, profilePath_});
    }
    phase_ = Phase::Done;
    actions.push_back({Kind::RecoveryDone});
    return actions;
}

ConnectActions ConnectAttempt::succeed() {
    if (createdProfile_ && !profilePath_.isEmpty()) {
        phase_ = Phase::Saving;
        return {{Kind::SaveProfile, profilePath_}};
    }
    return finish(WifiError::None);
}

ConnectActions ConnectAttempt::fail(WifiError error) {
    ConnectActions actions;
    if (createdProfile_ && !profilePath_.isEmpty()) actions.push_back({Kind::DeleteProfile, profilePath_});
    append(actions, finish(error));
    return actions;
}

ConnectActions ConnectAttempt::finish(WifiError error) {
    result_ = error;
    ConnectActions actions{{Kind::Finish, {}, 0, error}};
    // Recover only if the device actually left the previous network. A
    // request NetworkManager rejected outright never touched it.
    if (error != WifiError::None && !previousProfile_.isEmpty() && deviceState_ != device_state::kActivated) {
        phase_ = Phase::Recovering;
        actions.push_back({Kind::StartTimer, {}, kRecoveryDelayMs});
        return actions;
    }
    phase_ = Phase::Done;
    actions.push_back({Kind::RecoveryDone});
    return actions;
}

} // namespace pi_control::wifi
