#pragma once

#include "wifi/wifi_backend.h"

#include <QtGlobal>

#include <vector>

namespace pi_control::wifi {

// NM80211ApFlags and NM80211ApSecurityFlags bits used for classification.
inline constexpr quint32 kApFlagPrivacy = 0x1;
inline constexpr quint32 kKeyMgmtPsk = 0x100;
inline constexpr quint32 kKeyMgmt8021x = 0x200;
inline constexpr quint32 kKeyMgmtSae = 0x400;

struct SecurityClass {
    WifiSecurity security = WifiSecurity::Open;
    QString unsupportedReason{};
};

SecurityClass classifySecurity(quint32 flags, quint32 wpaFlags, quint32 rsnFlags);

// One access point as NetworkManager reports it.
struct AccessPointInfo {
    QString path;
    QByteArray ssidBytes;
    int strength = 0;
    quint32 flags = 0;
    quint32 wpaFlags = 0;
    quint32 rsnFlags = 0;
};

// Display text for SSID bytes; invalid UTF-8 becomes U+FFFD.
QString displaySsid(const QByteArray& ssidBytes);

// One network per SSID (its strongest access point), without empty SSIDs,
// sorted by strength and then SSID. activeSsid and savedSsids set the flags.
std::vector<WifiNetwork> mergeAccessPoints(const std::vector<AccessPointInfo>& accessPoints,
                                           const QByteArray& activeSsid, const std::vector<QByteArray>& savedSsids);

// The strongest access point path for ssidBytes, or "/" to let
// NetworkManager match by SSID.
QString strongestAccessPoint(const std::vector<AccessPointInfo>& accessPoints, const QByteArray& ssidBytes);

} // namespace pi_control::wifi
