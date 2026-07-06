#include "core/version.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace bf {

QString VersionInfo::displayString() const {
    return QString("%1 %2").arg(productName, version);
}

VersionService::VersionService(QString repoRoot) : repoRoot_(std::move(repoRoot)) {}

const QString& VersionService::repoRoot() const {
    return repoRoot_;
}

bool VersionService::load(VersionInfo* out, QString* errorMessage) const {
    if (out == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = "version output pointer is null";
        }
        return false;
    }

    QFile file(QDir(repoRoot_).filePath("version.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = parseError.errorString();
        }
        return false;
    }

    const QJsonObject obj = doc.object();
    out->schemaVersion = obj.value("schema_version").toInt();
    out->productName = obj.value("product_name").toString();
    out->version = obj.value("version").toString();
    out->releaseChannel = obj.value("release_channel").toString();
    out->ipcProtocolVersion = obj.value("ipc_protocol_version").toInt();
    return !out->productName.isEmpty() && !out->version.isEmpty() && out->ipcProtocolVersion > 0;
}

QString VersionService::detectRepoRoot() {
    const QString compiledRoot = QStringLiteral(BF_REPO_ROOT);
    if (QFile::exists(QDir(compiledRoot).filePath("version.json"))) {
        return QDir(compiledRoot).absolutePath();
    }

    const QString currentRoot = QDir::currentPath();
    if (QFile::exists(QDir(currentRoot).filePath("version.json"))) {
        return QDir(currentRoot).absolutePath();
    }

    QDir appDir(QCoreApplication::applicationDirPath());
    if (appDir.cd("../..") && QFile::exists(appDir.filePath("version.json"))) {
        return appDir.absolutePath();
    }

    return QDir(compiledRoot).absolutePath();
}

}  // namespace bf
