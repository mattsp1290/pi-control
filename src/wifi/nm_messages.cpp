#include "wifi/nm_messages.h"

#include "wifi/nm_flags.h"

#include <QDBusMetaType>
#include <QDBusObjectPath>

namespace pi_control::wifi::nm {

void registerTypes() {
    static const bool registered = [] {
        qDBusRegisterMetaType<Settings>();
        return true;
    }();
    Q_UNUSED(registered);
}

QDBusMessage buildCallMessage(const QString& path, const char* interface, const char* method) {
    return QDBusMessage::createMethodCall(kService, path, interface, method);
}

QDBusMessage buildAddAndActivateMessage(const QByteArray& ssidBytes, WifiSecurity security, const QString& password,
                                        const QString& devicePath, const QString& accessPointPath) {
    registerTypes();
    Settings settings;
    settings["connection"] = QVariantMap{{"id", displaySsid(ssidBytes)}, {"type", "802-11-wireless"}};
    settings["802-11-wireless"] = QVariantMap{{"ssid", ssidBytes}};
    if (security == WifiSecurity::Personal) {
        settings["802-11-wireless-security"] = QVariantMap{{"psk", password}};
    }
    auto message = buildCallMessage(kPath, kManager, "AddAndActivateConnection2");
    message << QVariant::fromValue(settings) << QVariant::fromValue(QDBusObjectPath(devicePath))
            << QVariant::fromValue(QDBusObjectPath(accessPointPath)) << QVariantMap{{"persist", "memory"}};
    return message;
}

QDBusMessage buildActivateMessage(const QString& profilePath, const QString& devicePath,
                                  const QString& accessPointPath) {
    auto message = buildCallMessage(kPath, kManager, "ActivateConnection");
    message << QVariant::fromValue(QDBusObjectPath(profilePath)) << QVariant::fromValue(QDBusObjectPath(devicePath))
            << QVariant::fromValue(QDBusObjectPath(accessPointPath));
    return message;
}

QDBusMessage buildDeactivateMessage(const QString& activePath) {
    auto message = buildCallMessage(kPath, kManager, "DeactivateConnection");
    message << QVariant::fromValue(QDBusObjectPath(activePath));
    return message;
}

QDBusMessage buildSaveMessage(const QString& profilePath) {
    return buildCallMessage(profilePath, kConnection, "Save");
}

QDBusMessage buildDeleteMessage(const QString& profilePath) {
    return buildCallMessage(profilePath, kConnection, "Delete");
}

QDBusMessage buildRequestScanMessage(const QString& devicePath) {
    auto message = buildCallMessage(devicePath, kWireless, "RequestScan");
    message << QVariantMap{};
    return message;
}

QDBusMessage buildGetPropertyMessage(const QString& path, const char* interface, const QString& name) {
    auto message = buildCallMessage(path, kProperties, "Get");
    message << QString::fromLatin1(interface) << name;
    return message;
}

QDBusMessage buildGetAllMessage(const QString& path, const char* interface) {
    auto message = buildCallMessage(path, kProperties, "GetAll");
    message << QString::fromLatin1(interface);
    return message;
}

} // namespace pi_control::wifi::nm
