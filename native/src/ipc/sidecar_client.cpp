#include "ipc/sidecar_client.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

namespace bf {
namespace {

AppError appErrorFromJson(const QJsonObject& error) {
    return makeError(
        error.value("code").toString("internal.unexpected"),
        error.value("params").toObject(),
        error.value("retryable").toBool(false));
}

}  // namespace

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

    const SidecarResponse response = request("hello", {}, timeoutMs);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }

    const QJsonObject payload = response.result;
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

SidecarResponse SidecarClient::request(const QString& method, const QJsonObject& params, int timeoutMs) {
    SidecarResponse result;
    if (process_.state() == QProcess::NotRunning) {
        result.error = makeError("ipc.sidecar_exited", {{"exit_code", process_.exitCode()}});
        return result;
    }

    const QJsonObject request{{"id", method + "-1"}, {"method", method}, {"params", params}};
    if (!writeJsonLine(request, timeoutMs)) {
        result.error = makeError("ipc.sidecar_timeout", {{"operation", method + "_write"}, {"timeout_ms", timeoutMs}});
        return result;
    }

    const QByteArray line = readJsonLine(timeoutMs);
    if (line.isEmpty()) {
        if (process_.state() == QProcess::NotRunning) {
            result.error = makeError("ipc.sidecar_exited", {{"exit_code", process_.exitCode()}});
        } else {
            result.error = makeError("ipc.sidecar_timeout", {{"operation", method + "_read"}, {"timeout_ms", timeoutMs}});
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
        result.error = appErrorFromJson(response.value("error").toObject());
        return result;
    }

    result.ok = true;
    result.result = response.value("result").toObject();
    return result;
}

SidecarOpenResult SidecarClient::openFile(const QString& path, int timeoutMs) {
    SidecarOpenResult result;
    const SidecarResponse response = request("open", {{"path", path}}, timeoutMs);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }

    const QJsonObject payload = response.result;
    result.handle = payload.value("handle").toString();
    result.fs = payload.value("fs").toDouble();
    result.nChannels = payload.value("n_channels").toInt();
    result.nSamples = payload.value("n_samples").toInt();
    result.duration = payload.value("duration").toDouble();
    result.format = payload.value("format").toString();
    const QJsonArray channels = payload.value("ch_names").toArray();
    for (const QJsonValue& value : channels) {
        result.chNames.push_back(value.toString());
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
