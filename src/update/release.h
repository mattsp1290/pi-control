#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <compare>
#include <optional>
#include <variant>

namespace pi_control::update {

// A release version: X.Y.Z with no sign, leading zero or suffix.
struct Version {
    quint32 major = 0;
    quint32 minor = 0;
    quint32 patch = 0;

    auto operator<=>(const Version&) const = default;
    QString toString() const;
};

std::optional<Version> parseVersion(QStringView text);

// The release a manifest names once it passes the same checks
// distribution/install.sh applies before it installs anything, or why it does not.
std::variant<Version, QString> parseManifest(const QByteArray& json);

} // namespace pi_control::update
