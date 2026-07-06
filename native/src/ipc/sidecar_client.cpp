#include "ipc/sidecar_client.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

namespace bf {

SidecarClient::SidecarClient(QString repoRoot) : repoRoot_(std::move(repoRoot)) {}

SidecarClient::~SidecarClient() {
    stop();
}

bool SidecarClient::writeJsonLine(const QJsonObject& object, int timeoutMs) {
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact) + "\n";
    process_.write(payload);
    return process_.waitForBytesWritten(timeoutMs);
}

QByteArray SidecarClient::readJsonLine(int timeoutMs) {
    while (!process_.canReadLine()) {
        if (!process_.waitForReadyRead(timeoutMs)) {
            return {};
        }
    }
    return process_.readLine().trimmed();
}

SidecarHelloResult SidecarClient::startAndHello(const VersionInfo& version, int timeoutMs) {
    SidecarHelloResult result;

    process_.setProgram("python3");
    process_.setArguments({"-m", "boundfree_sidecar", "--stdio"});
    process_.setWorkingDirectory(repoRoot_);
    process_.setProcessChannelMode(QProcess::SeparateChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString sidecarPath = QDir(repoRoot_).filePath("sidecar");
    const QString existingPythonPath = env.value("PYTHONPATH");
    env.insert("PYTHONPATH", existingPythonPath.isEmpty() ? sidecarPath : sidecarPath + ":" + existingPythonPath);
    process_.setProcessEnvironment(env);

    process_.start();
    if (!process_.waitForStarted(timeoutMs)) {
        result.error = makeError("ipc.sidecar_start_failed", {{"reason", process_.errorString()}});
        return result;
    }

    const QJsonObject request{{"id", "hello-1"}, {"method", "hello"}, {"params", QJsonObject{}}};
    if (!writeJsonLine(request, timeoutMs)) {
        result.error = makeError("ipc.sidecar_timeout", {{"operation", "hello_write"}, {"timeout_ms", timeoutMs}});
        return result;
    }

    const QByteArray line = readJsonLine(timeoutMs);
    if (line.isEmpty()) {
        if (process_.state() == QProcess::NotRunning) {
            result.error = makeError("ipc.sidecar_exited", {{"exit_code", process_.exitCode()}});
        } else {
            result.error = makeError("ipc.sidecar_timeout", {{"operation", "hello_read"}, {"timeout_ms", timeoutMs}});
        }
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = makeError("protocol.invalid_json");
        return result;
    }

    const QJsonObject response = doc.object();
    if (!response.value("ok").toBool(false)) {
        const QJsonObject error = response.value("error").toObject();
        result.error = makeError(
            error.value("code").toString("internal.unexpected"),
            error.value("params").toObject(),
            error.value("retryable").toBool(false));
        return result;
    }

    const QJsonObject payload = response.value("result").toObject();
    result.productVersion = payload.value("product_version").toString();
    result.ipcProtocolVersion = payload.value("ipc_protocol_version").toInt();
    if (result.ipcProtocolVersion != version.ipcProtocolVersion) {
        result.error = makeError(
            "protocol.version_mismatch",
            {{"expected", version.ipcProtocolVersion}, {"actual", result.ipcProtocolVersion}});
        return result;
    }

    result.ok = true;
    return result;
}

void SidecarClient::stop() {
    if (process_.state() == QProcess::NotRunning) {
        return;
    }
    process_.closeWriteChannel();
    if (!process_.waitForFinished(1000)) {
        process_.terminate();
    }
    if (!process_.waitForFinished(1000)) {
        process_.kill();
        process_.waitForFinished(1000);
    }
}

}  // namespace bf
