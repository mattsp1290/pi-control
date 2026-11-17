#pragma once

#include "wifi/wifi_backend.h"

#include <QString>

#include <optional>
#include <vector>

namespace pi_control::wifi {

// NMDeviceState values.
namespace device_state {
inline constexpr quint32 kDisconnected = 30;
inline constexpr quint32 kPrepare = 40;
inline constexpr quint32 kSecondaries = 90;
inline constexpr quint32 kActivated = 100;
inline constexpr quint32 kFailed = 120;
} // namespace device_state

// NMActiveConnectionState values.
namespace active_state {
inline constexpr quint32 kActivated = 2;
inline constexpr quint32 kDeactivated = 4;
} // namespace active_state

// NMDeviceStateReason and NMActiveConnectionStateReason values used to
// classify a failed join (nm-dbus-interface.h, NetworkManager 1.42-1.52).
namespace reason {
inline constexpr quint32 kDeviceNoSecrets = 7;
inline constexpr quint32 kDeviceSsidNotFound = 53;
inline constexpr quint32 kActiveNoSecrets = 9;
} // namespace reason

inline constexpr int kConnectTimeoutMs = 45000;
// How long NetworkManager gets to rejoin the previous network on its own.
inline constexpr int kRecoveryDelayMs = 15000;
// How long an explicit return to the previous network may take.
inline constexpr int kRecoveryRejoinMs = 30000;

// What a failed join means to the user. A missing or rejected secret means
// the password was wrong: the kiosk registers no secret agent, so
// NetworkManager's request for a new secret fails with "no secrets".
WifiError classifyConnectFailure(std::optional<quint32> deviceReason, std::optional<quint32> activeReason);

// The error a failed D-Bus call maps to.
WifiError classifyCallError(const QString& errorName);

struct ConnectAction {
    enum class Kind {
        SubscribeActive,  // path: active connection to watch for StateChanged.
        ReadActiveState,  // path: active connection whose State to read.
        SaveProfile,      // path: in-memory profile to write to disk.
        DeleteProfile,    // path: profile to delete.
        Deactivate,       // path: active connection to stop.
        ActivatePrevious, // path: profile that was active before the attempt.
        StartTimer,       // milliseconds; replaces any running timer.
        Finish,           // error: the attempt's one result.
        RecoveryDone,     // The attempt is over; a new one may start.
    };
    Kind kind;
    QString path{};
    int milliseconds = 0;
    WifiError error = WifiError::None;

    bool operator==(const ConnectAction&) const = default;
};

using ConnectActions = std::vector<ConnectAction>;

// One join attempt as a pure state machine. NmWifiBackend feeds it D-Bus
// replies, signals and timer expiry, and executes the actions it returns.
//
// The outcome comes only from the attempt's own active connection (its
// state, or its disappearance). Device states can belong to another
// activation (NetworkManager's autoconnect, or the previous network being
// torn down), so they only supply the failure reason and drive recovery.
class ConnectAttempt {
public:
    enum class Call { AddAndActivate, Activate, ReadActiveState, Save };
    enum class Phase { Starting, Waiting, Saving, Recovering, Rejoining, Done };

    // createdProfile: the attempt creates a new in-memory profile.
    // previousProfile: profile active on the device before the attempt.
    // deviceState: the device's NMDeviceState when the attempt starts.
    ConnectAttempt(bool createdProfile, QString previousProfile, quint32 deviceState = 0);

    ConnectActions addReplied(const QString& profilePath, const QString& activePath);
    ConnectActions activateReplied(const QString& activePath);
    ConnectActions callFailed(Call call, const QString& errorName);
    ConnectActions activeStateRead(quint32 state);
    ConnectActions activeStateChanged(quint32 state, quint32 reason);
    ConnectActions deviceStateChanged(quint32 newState, quint32 oldState, quint32 reason);
    ConnectActions saveReplied();
    ConnectActions timerFired(); // Attempt timeout or a recovery deadline, by phase.
    ConnectActions serviceVanished();
    ConnectActions cancelled();

    Phase phase() const { return phase_; }
    std::optional<WifiError> result() const { return result_; }
    bool isDone() const { return phase_ == Phase::Done; }

private:
    ConnectActions replied(const QString& activePath);
    ConnectActions succeed();
    ConnectActions fail(WifiError error);
    ConnectActions finish(WifiError error);
    ConnectActions recoveryTimedOut();
    ConnectActions recovered();
    bool finished() const { return result_.has_value(); }

    bool createdProfile_;
    QString previousProfile_;
    QString profilePath_;
    QString activePath_;
    Phase phase_ = Phase::Starting;
    std::optional<WifiError> result_;
    quint32 deviceState_;
    std::optional<quint32> failureReason_; // Device reason seen while waiting.
};

} // namespace pi_control::wifi
