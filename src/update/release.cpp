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
    const auto match = grammar.matchView(text);
    if (!match.hasMatch()) return std::nullopt;
    return Version{match.capturedView(1).toUInt(), match.capturedView(2).toUInt(), match.capturedView(3).toUInt()};
}

ManifestResult parseManifest(const QByteArray& json) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!document.isObject()) return {.version = std::nullopt, .error = "manifest is not a JSON object"};
    const QJsonObject manifest = document.object();
    if (manifest.value("schema") != QJsonValue(1)) return {.version = std::nullopt, .error = "unsupported manifest schema"};
    const auto version = parseVersion(manifest.value("version").toString());
    if (!version) return {.version = std::nullopt, .error = "invalid manifest version"};
    static const QRegularExpression digest("^[0-9a-f]{64}$");
    const QString sha256 = manifest.value("sha256").toObject().value("linux-aarch64").toString();
    if (!digest.match(sha256).hasMatch()) return {.version = std::nullopt, .error = "invalid manifest checksum"};
    return {.version = version, .error = {}};
}

} // namespace pi_control::update
