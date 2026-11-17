#pragma once

#include <QString>

#include <vector>

namespace pi_control::system {

struct InterfaceAddress {
    QString interface;
    QString address;
};

// IPv4 addresses of every up, running, non-loopback interface.
std::vector<InterfaceAddress> currentIpv4Addresses();

} // namespace pi_control::system
