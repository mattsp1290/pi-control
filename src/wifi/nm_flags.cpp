#include "wifi/nm_flags.h"

#include <algorithm>
#include <map>

namespace pi_control::wifi {

const char* toString(WifiError error) {
    switch (error) {
    case WifiError::None: return "None";
    case WifiError::WrongPassword: return "WrongPassword";
    case WifiError::NotPermitted: return "NotPermitted";
    case WifiError::NotFound: return "NotFound";
    case WifiError::Timeout: return "Timeout";
    case WifiError::Unavailable: return "Unavailable";
    case WifiError::Failed: return "Failed";
    }
    return "Failed";
}

SecurityClass classifySecurity(quint32 flags, quint32 wpaFlags, quint32 rsnFlags) {
    const quint32 keyMgmt = wpaFlags | rsnFlags;
    if (keyMgmt & kKeyMgmtPsk) return {WifiSecurity::Personal};
    if (rsnFlags & kKeyMgmtSae) return {WifiSecurity::Personal};
    if (keyMgmt & kKeyMgmt8021x) return {WifiSecurity::Unsupported, "Enterprise"};
    if ((flags & kApFlagPrivacy) && wpaFlags == 0 && rsnFlags == 0) return {WifiSecurity::Unsupported, "WEP"};
    if (keyMgmt != 0) return {WifiSecurity::Unsupported, "Unknown security"};
    return {WifiSecurity::Open};
}

QString displaySsid(const QByteArray& ssidBytes) {
    return QString::fromUtf8(ssidBytes); // Invalid sequences become U+FFFD.
}

std::vector<WifiNetwork> mergeAccessPoints(const std::vector<AccessPointInfo>& accessPoints,
                                           const QByteArray& activeSsid, const std::vector<QByteArray>& savedSsids) {
    std::map<QByteArray, const AccessPointInfo*> strongest;
    for (const auto& ap : accessPoints) {
        if (ap.ssidBytes.isEmpty()) continue;
        auto& best = strongest[ap.ssidBytes];
        if (!best || ap.strength > best->strength) best = &ap;
    }
    std::vector<WifiNetwork> networks;
    networks.reserve(strongest.size());
    for (const auto& [ssid, ap] : strongest) {
        const SecurityClass security = classifySecurity(ap->flags, ap->wpaFlags, ap->rsnFlags);
        WifiNetwork network;
        network.ssid = displaySsid(ssid);
        network.ssidBytes = ssid;
        network.strength = std::clamp(ap->strength, 0, 100);
        network.security = security.security;
        network.unsupportedReason = security.unsupportedReason;
        network.active = !activeSsid.isEmpty() && ssid == activeSsid;
        network.saved = std::find(savedSsids.begin(), savedSsids.end(), ssid) != savedSsids.end();
        networks.push_back(std::move(network));
    }
    std::sort(networks.begin(), networks.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        if (a.strength != b.strength) return a.strength > b.strength;
        const int byName = QString::compare(a.ssid, b.ssid, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.ssidBytes < b.ssidBytes;
    });
    return networks;
}

QString strongestAccessPoint(const std::vector<AccessPointInfo>& accessPoints, const QByteArray& ssidBytes) {
    const AccessPointInfo* best = nullptr;
    for (const auto& ap : accessPoints) {
        if (ap.ssidBytes == ssidBytes && (!best || ap.strength > best->strength)) best = &ap;
    }
    return best ? best->path : QStringLiteral("/");
}

} // namespace pi_control::wifi
