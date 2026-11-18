#pragma once

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

// A file:// release tree whose latest manifest names version.
struct ReleaseTree {
    explicit ReleaseTree(const QByteArray& version) {
        write(QByteArray(R"({"schema": 1, "version": ")") + version
              + R"(", "sha256": {"linux-aarch64": ")" + QByteArray(64, 'a') + R"("}})");
    }
    void write(const QByteArray& manifest) const {
        QDir().mkpath(dir.filePath("latest/download"));
        QFile file(dir.filePath("latest/download/pi-control-manifest.json"));
        if (!file.open(QIODevice::WriteOnly)) qFatal("cannot write the manifest");
        file.write(manifest);
    }
    QUrl base() const { return QUrl::fromLocalFile(dir.path()); }

    QTemporaryDir dir;
};
