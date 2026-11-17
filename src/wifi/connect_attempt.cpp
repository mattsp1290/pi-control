#include "wifi/connect_attempt.h"

namespace pi_control::wifi {

namespace {

using Kind = ConnectAction::Kind;

bool isActivating(quint32 state) {
    return state >= device_state::kPrepare && state <= device_state::kSecondaries;
}

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

ConnectAttempt::ConnectAttempt(bool createdProfile, QString previousProfile)
    : createdProfile_(createdProfile), previousProfile_(std::move(previousProfile)) {}

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
    if (earlyDeviceOutcome_ == device_state::kActivated) return succeed();
    if (earlyDeviceOutcome_ == device_state::kFailed) {
        return fail(classifyConnectFailure(lastDeviceReason_, std::nullopt));
    }
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
        // The active connection may already be gone; the device signal or
        // the timeout decides.
        return {};
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
    if (state == active_state::kDeactivated) {
        return fail(classifyConnectFailure(lastDeviceReason_, std::nullopt));
    }
    return {};
}

ConnectActions ConnectAttempt::activeStateChanged(quint32 state, quint32 reason) {
    if (phase_ != Phase::Waiting) return {};
    if (state == active_state::kActivated) return succeed();
    if (state == active_state::kDeactivated) return fail(classifyConnectFailure(lastDeviceReason_, reason));
    return {};
}

ConnectActions ConnectAttempt::deviceStateChanged(quint32 newState, quint32, quint32 reason) {
    lastDeviceState_ = newState;
    if (reason != 0) lastDeviceReason_ = reason;
    if (isActivating(newState)) sawActivating_ = true;
    const bool activated = newState == device_state::kActivated && sawActivating_;
    const bool failed = newState == device_state::kFailed
                        || (newState == device_state::kDisconnected && sawActivating_);
    switch (phase_) {
    case Phase::Starting:
        if (activated) earlyDeviceOutcome_ = device_state::kActivated;
        else if (failed) earlyDeviceOutcome_ = device_state::kFailed;
        return {};
    case Phase::Waiting:
        if (activated) return succeed();
        if (failed) return fail(classifyConnectFailure(lastDeviceReason_, std::nullopt));
        return {};
    case Phase::Recovering:
        if (newState == device_state::kActivated) {
            phase_ = Phase::Done;
            return {{Kind::RecoveryDone}};
        }
        return {};
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
    case Phase::Starting:
    case Phase::Done:
        return {};
    }
    return {};
}

ConnectActions ConnectAttempt::recoveryTimedOut() {
    phase_ = Phase::Done;
    // Already back on a network, or on its way: do not interrupt it.
    if (lastDeviceState_ && *lastDeviceState_ >= device_state::kPrepare
        && *lastDeviceState_ <= device_state::kActivated) {
        return {{Kind::RecoveryDone}};
    }
    return {{Kind::ActivatePrevious, previousProfile_}, {Kind::RecoveryDone}};
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
    if (!finished() && createdProfile_ && !profilePath_.isEmpty()) {
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
    if (error != WifiError::None && !previousProfile_.isEmpty()) {
        phase_ = Phase::Recovering;
        actions.push_back({Kind::StartTimer, {}, kRecoveryDelayMs});
        return actions;
    }
    phase_ = Phase::Done;
    actions.push_back({Kind::RecoveryDone});
    return actions;
}

} // namespace pi_control::wifi
