#pragma once

#include "wifi/wifi_backend.h"

#include <QDBusMessage>
#include <QMap>
#include <QMetaType>
#include <QVariantMap>

namespace pi_control::wifi::nm {

inline constexpr auto kService = "org.freedesktop.NetworkManager";
inline constexpr auto kPath = "/org/freedesktop/NetworkManager";
inline constexpr auto kSettingsPath = "/org/freedesktop/NetworkManager/Settings";
inline constexpr auto kManager = "org.freedesktop.NetworkManager";
inline constexpr auto kDevice = "org.freedesktop.NetworkManager.Device";
inline constexpr auto kWireless = "org.freedesktop.NetworkManager.Device.Wireless";
inline constexpr auto kAccessPoint = "org.freedesktop.NetworkManager.AccessPoint";
inline constexpr auto kActive = "org.freedesktop.NetworkManager.Connection.Active";
inline constexpr auto kSettings = "org.freedesktop.NetworkManager.Settings";
inline constexpr auto kConnection = "org.freedesktop.NetworkManager.Settings.Connection";
inline constexpr auto kProperties = "org.freedesktop.DBus.Properties";

// a{sa{sv}}: NetworkManager connection settings.
using Settings = QMap<QString, QVariantMap>;

// Registers the D-Bus types above. Safe to call more than once.
void registerTypes();

// A new profile for ssidBytes that lives in memory until saved. For
// Personal networks only the passphrase is sent; NetworkManager completes
// the key management from the access point.
QDBusMessage buildAddAndActivateMessage(const QByteArray& ssidBytes, WifiSecurity security, const QString& password,
                                        const QString& devicePath, const QString& accessPointPath);
QDBusMessage buildActivateMessage(const QString& profilePath, const QString& devicePath,
                                  const QString& accessPointPath);
QDBusMessage buildDeactivateMessage(const QString& activePath);
QDBusMessage buildSaveMessage(const QString& profilePath);
QDBusMessage buildDeleteMessage(const QString& profilePath);
QDBusMessage buildRequestScanMessage(const QString& devicePath);
QDBusMessage buildGetPropertyMessage(const QString& path, const char* interface, const QString& name);
QDBusMessage buildGetAllMessage(const QString& path, const char* interface);
QDBusMessage buildCallMessage(const QString& path, const char* interface, const char* method);

} // namespace pi_control::wifi::nm

Q_DECLARE_METATYPE(pi_control::wifi::nm::Settings)
