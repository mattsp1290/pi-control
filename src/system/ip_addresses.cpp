#include "system/ip_addresses.h"

#include <QHostAddress>
#include <QNetworkInterface>

namespace pi_control::system {

std::vector<InterfaceAddress> currentIpv4Addresses() {
    std::vector<InterfaceAddress> result;
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                result.push_back({interface.name(), entry.ip().toString()});
            }
        }
    }
    return result;
}

} // namespace pi_control::system
