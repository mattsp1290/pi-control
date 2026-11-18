#include "update/release.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace pi_control::update {

QString Version::toString() const { return QString("%1.%2.%3").arg(major).arg(minor).arg(patch); }

std::optional<Version> parseVersion(QStringView text) {
    // Nine digits always fit in quint32.
    static const QRegularExpression grammar(
        R"(^(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})$)");
    const auto match = grammar.match(text.toString()); // matchView() needs Qt 6.5; Bookworm has 6.4.
    if (!match.hasMatch()) return std::nullopt;
    return Version{match.capturedView(1).toUInt(), match.capturedView(2).toUInt(), match.capturedView(3).toUInt()};
}

std::variant<Version, QString> parseManifest(const QByteArray& json) {
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) return QString("manifest is not a JSON object");
    const QJsonObject manifest = document.object();
    if (manifest.value("schema") != QJsonValue(1)) return QString("unsupported manifest schema");
    const auto version = parseVersion(manifest.value("version").toString());
    if (!version) return QString("invalid manifest version");
    static const QRegularExpression digest("^[0-9a-f]{64}$");
    const QString sha256 = manifest.value("sha256").toObject().value("linux-aarch64").toString();
    if (!digest.match(sha256).hasMatch()) return QString("invalid manifest checksum");
    return *version;
}

} // namespace pi_control::update
