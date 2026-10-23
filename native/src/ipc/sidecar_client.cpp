#include "ipc/sidecar_client.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QElapsedTimer>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace bf {
namespace {

constexpr quint8 kJsonFrame = 1;
constexpr quint8 kBinaryFrame = 2;
constexpr quint32 kMaxPayloadBytes = 256u * 1024u * 1024u;

AppError appErrorFromJson(const QJsonObject& error) {
    AppError result = makeError(
        error.value("code").toString("internal.unexpected"),
        error.value("params").toObject(),
        error.value("retryable").toBool(false));
    result.traceId = error.value("trace_id").toString(result.traceId);
    return result;
}

AppError invalidPayload(const QString& field, const QString& reason) {
    return makeError("protocol.invalid_params", {{"field", field}, {"reason", reason}});
}


bool jsonValueToNonNegativeInt(const QJsonValue& value, int* out) {
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    *out = static_cast<int>(number);
    return true;
}

SidecarRecordResult parseRecordResult(const SidecarResponse& response) {
    SidecarRecordResult result;
    if (!response.ok) {
        result.error = response.error;
        return result;
    }
    result.ok = true;
    result.accepted = response.result.value("accepted").toBool(false);
    result.backlogChunks = response.result.value("backlog_chunks").toInt();
    result.samplesWritten = response.result.value("samples_written").toInt();
    result.droppedChunks = response.result.value("dropped_chunks").toInt();
    result.droppedEvents = response.result.value("dropped_events").toInt();
    result.paths = response.result.value("paths").toObject();
    return result;
}

QJsonArray eventsToJson(const QVector<SidecarEvent>& events) {
    QJsonArray markers;
    for (const SidecarEvent& event : events) {
        markers.append(QJsonObject{{"time", event.time}, {"label", event.label}});
    }
    return markers;
}

}  // namespace

SidecarClient::SidecarClient(QString repoRoot) : repoRoot_(std::move(repoRoot)) {}

SidecarClient::~SidecarClient() {
    stop();
}

bool SidecarClient::writeFrame(quint8 frameType, const QByteArray& payload, int timeoutMs) {
    if (payload.size() < 0 || static_cast<quint64>(payload.size()) > kMaxPayloadBytes) {
        return false;
    }
    QByteArray header(5, Qt::Uninitialized);
    header[0] = static_cast<char>(frameType);
    qToLittleEndian<quint32>(static_cast<quint32>(payload.size()), reinterpret_cast<uchar*>(header.data() + 1));
    if (process_.write(header) != header.size() || process_.write(payload) != payload.size()) {
        return false;
    }
    QElapsedTimer elapsed;
    elapsed.start();
    while (process_.bytesToWrite() > 0) {
        if ((cancelled_ && cancelled_()) || elapsed.elapsed() >= timeoutMs ||
            process_.state() == QProcess::NotRunning) return false;
        process_.waitForBytesWritten(std::max(1, std::min(100, timeoutMs - static_cast<int>(elapsed.elapsed()))));
    }
    return true;
}

bool SidecarClient::readFrame(quint8* frameType, QByteArray* payload, int timeoutMs) {
    QElapsedTimer elapsed;
    elapsed.start();
    const auto waitForData = [&]() {
        while (process_.bytesAvailable() == 0) {
            if ((cancelled_ && cancelled_()) || elapsed.elapsed() >= timeoutMs ||
                process_.state() == QProcess::NotRunning) return false;
            process_.waitForReadyRead(std::max(1, std::min(100, timeoutMs - static_cast<int>(elapsed.elapsed()))));
        }
        return !(cancelled_ && cancelled_());
    };
    QByteArray header;
    while (header.size() < 5) {
        if (!waitForData()) {
            return false;
        }
        header += process_.read(5 - header.size());
    }
    *frameType = static_cast<quint8>(header[0]);
    const quint32 payloadSize = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header.constData() + 1));
    if ((*frameType != kJsonFrame && *frameType != kBinaryFrame) || payloadSize > kMaxPayloadBytes) {
        return false;
    }
    payload->clear();
    payload->reserve(static_cast<int>(payloadSize));
    while (static_cast<quint32>(payload->size()) < payloadSize) {
        if (!waitForData()) {
            return false;
        }
        *payload += process_.read(static_cast<qint64>(payloadSize - static_cast<quint32>(payload->size())));
    }
    return true;
}

SidecarResponse SidecarClient::exchange(
    const QString& method,
    const QJsonObject& params,
    const QByteArray& outgoingPayload,
    bool expectBinaryPayload,
    QByteArray* incomingPayload,
    int timeoutMs) {
    SidecarResponse result;
    if (process_.state() == QProcess::NotRunning) {
        result.error = makeError("ipc.sidecar_exited", {{"exit_code", process_.exitCode()}});
        return result;
    }
    const QJsonObject requestObject{{"id", method + "-1"}, {"method", method}, {"params", params}};
    const QByteArray requestBytes = QJsonDocument(requestObject).toJson(QJsonDocument::Compact);
    if (!writeFrame(kJsonFrame, requestBytes, timeoutMs) ||
        (!outgoingPayload.isEmpty() && !writeFrame(kBinaryFrame, outgoingPayload, timeoutMs))) {
        result.error = makeError("ipc.sidecar_timeout", {{"operation", method + "_write"}, {"timeout_ms", timeoutMs}});
        return result;
    }

    quint8 responseType = 0;
    QByteArray responseBytes;
    if (!readFrame(&responseType, &responseBytes, timeoutMs)) {
        result.error = process_.state() == QProcess::NotRunning
                           ? makeError("ipc.sidecar_exited", {{"exit_code", process_.exitCode()}})
                           : makeError("ipc.sidecar_timeout", {{"operation", method + "_read"}, {"timeout_ms", timeoutMs}});
        return result;
    }
    if (responseType != kJsonFrame) {
        result.error = makeError("ipc.invalid_frame", {{"reason", "expected JSON response frame"}});
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = makeError("protocol.invalid_json");
        return result;
    }
    const QJsonObject response = document.object();
    if (!response.value("ok").toBool(false)) {
        result.error = appErrorFromJson(response.value("error").toObject());
        return result;
    }
    result.ok = true;
    result.result = response.value("result").toObject();
    if (expectBinaryPayload) {
        if (incomingPayload == nullptr) {
            result.ok = false;
            result.error = makeError("ipc.invalid_frame", {{"reason", "binary response target is missing"}});
            return result;
        }
        quint8 payloadType = 0;
        if (!readFrame(&payloadType, incomingPayload, timeoutMs) || payloadType != kBinaryFrame) {
            result.ok = false;
            result.error = makeError("ipc.invalid_frame", {{"reason", "expected binary payload frame"}});
        }
    }
    return result;
}

SidecarHelloResult SidecarClient::startAndHello(const VersionInfo& version, int timeoutMs) {
    SidecarHelloResult result;
    const QString configuredExecutable = qEnvironmentVariable("BF_SIDECAR_EXECUTABLE");
#ifdef Q_OS_WIN
    const QString bundledName = QStringLiteral("boundfree_sidecar.exe");
#else
    const QString bundledName = QStringLiteral("boundfree_sidecar");
#endif
    const QString bundledExecutable = QDir(repoRoot_).filePath(QString("sidecar/%1").arg(bundledName));
    const QString candidateExecutable = configuredExecutable.isEmpty() ? bundledExecutable : configuredExecutable;
    const QFileInfo candidateInfo(candidateExecutable);
    const bool useBundled = candidateInfo.isFile() && candidateInfo.isExecutable();
    if (useBundled) {
        process_.setProgram(candidateExecutable);
        process_.setArguments({"--stdio"});
    } else {
        const QString configuredPython = qEnvironmentVariable("BF_PYTHON_EXECUTABLE");
#ifdef Q_OS_WIN
        process_.setProgram(configuredPython.isEmpty() ? QStringLiteral("python") : configuredPython);
#else
        process_.setProgram(configuredPython.isEmpty() ? QStringLiteral("python3") : configuredPython);
#endif
        process_.setArguments({"-m", "boundfree_sidecar", "--stdio"});
    }
    process_.setWorkingDirectory(repoRoot_);
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString sidecarPath = QDir(repoRoot_).filePath("sidecar");
    const QString existingPythonPath = environment.value("PYTHONPATH");
    if (!useBundled) {
        environment.insert("PYTHONPATH", existingPythonPath.isEmpty() ? sidecarPath : sidecarPath + QDir::listSeparator() + existingPythonPath);
    }
    environment.insert("BF_RESOURCE_ROOT", repoRoot_);
    environment.insert("PYTHONIOENCODING", "utf-8");
    environment.insert("PYTHONUTF8", "1");
    process_.setProcessEnvironment(environment);
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
    result.productVersion = response.result.value("product_version").toString();
    for (const auto& capability : response.result.value("analysis_capabilities").toArray())
        result.analysisCapabilities.append(capability.toString());
    result.ipcProtocolVersion = response.result.value("ipc_protocol_version").toInt();
    if (result.ipcProtocolVersion != version.ipcProtocolVersion) {
        result.error = makeError("protocol.version_mismatch", {{"expected", version.ipcProtocolVersion}, {"actual", result.ipcProtocolVersion}});
        return result;
    }
    result.ok = true;
    return result;
}

SidecarResponse SidecarClient::request(const QString& method, const QJsonObject& params, int timeoutMs) {
    return exchange(method, params, {}, false, nullptr, timeoutMs);
}

SidecarResponse SidecarClient::requestBinary(const QString& method, const QJsonObject& params,
                                           const QByteArray& outgoing, QByteArray* incoming,
                                           int timeoutMs) {
    return exchange(method, params, outgoing, incoming != nullptr, incoming, timeoutMs);
}

SidecarOpenResult SidecarClient::openFile(const QString& path, int timeoutMs) {
    SidecarOpenResult result;
    result.path = path;
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
    for (const QJsonValue& value : payload.value("ch_names").toArray()) {
        result.chNames.push_back(value.toString());
    }
    for (const QJsonValue& value : payload.value("events").toArray()) {
        const QJsonObject event = value.toObject();
        const double time = event.value("time").toDouble(std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(time) && time >= 0.0 && event.value("label").isString()) {
            result.events.push_back({time, event.value("label").toString()});
        }
    }
    result.ok = true;
    return result;
}

SidecarResponse SidecarClient::exportMarkersCsv(const QString& path, const QVector<SidecarEvent>& events, int timeoutMs) {
    return request("export_markers_csv", {{"path", path}, {"markers", eventsToJson(events)}}, timeoutMs);
}

SidecarSliceResult SidecarClient::getSlice(const QString& handle, double t0, double t1, int timeoutMs) {
    QByteArray payloadBytes;
    const SidecarResponse response = exchange(
        "get_slice", {{"handle", handle}, {"t0", t0}, {"t1", t1}}, {}, true, &payloadBytes, timeoutMs);
    if (!response.ok) {
        SidecarSliceResult result;
        result.error = response.error;
        return result;
    }
    return parseSliceBinaryPayload(response.result, payloadBytes);
}

SidecarRecordOpenResult SidecarClient::recordOpen(
    double fs,
    const QStringList& chNames,
    const QString& outputDir,
    const QJsonObject& metadata,
    int maxQueueChunks,
    int timeoutMs) {
    SidecarRecordOpenResult result;
    const SidecarResponse response = request(
        "record_open",
        {{"fs", fs}, {"ch_names", QJsonArray::fromStringList(chNames)}, {"out_dir", outputDir},
         {"meta", metadata}, {"formats", QJsonArray{"edf", "hdf5"}}, {"max_queue_chunks", maxQueueChunks}},
        timeoutMs);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }
    result.ok = true;
    result.handle = response.result.value("handle").toString();
    result.paths = response.result.value("paths").toObject();
    return result;
}

SidecarRecordResult SidecarClient::recordWrite(
    const QString& handle,
    const std::vector<std::vector<float>>& data,
    int timeoutMs) {
    SidecarRecordResult invalid;
    if (data.empty() || data.front().empty()) {
        invalid.error = invalidPayload("shape", "recording data must not be empty");
        return invalid;
    }
    const int nChannels = static_cast<int>(data.size());
    const int nSamples = static_cast<int>(data.front().size());
    QByteArray payload;
    payload.resize(nChannels * nSamples * static_cast<int>(sizeof(float)));
    char* destination = payload.data();
    for (const auto& channel : data) {
        if (static_cast<int>(channel.size()) != nSamples) {
            invalid.error = invalidPayload("shape", "recording rows must have equal length");
            return invalid;
        }
        for (float sample : channel) {
            quint32 bits = 0;
            static_assert(sizeof(bits) == sizeof(sample));
            std::memcpy(&bits, &sample, sizeof(bits));
            qToLittleEndian(bits, reinterpret_cast<uchar*>(destination));
            destination += sizeof(bits);
        }
    }
    const SidecarResponse response = exchange(
        "record_write", {{"handle", handle}, {"shape", QJsonArray{nChannels, nSamples}}}, payload, false, nullptr, timeoutMs);
    return parseRecordResult(response);
}

SidecarRecordResult SidecarClient::recordEvent(const QString& handle, double time, const QString& label, int timeoutMs) {
    return parseRecordResult(request("record_event", {{"handle", handle}, {"time", time}, {"label", label}}, timeoutMs));
}

SidecarRecordResult SidecarClient::recordClose(const QString& handle, int timeoutMs) {
    return parseRecordResult(request("record_close", {{"handle", handle}}, timeoutMs));
}

SidecarExportResult SidecarClient::exportSegment(
    const QString& sourcePath,
    double t0,
    double t1,
    const QString& outputPath,
    const QVector<SidecarEvent>& events,
    int timeoutMs) {
    SidecarExportResult result;
    const SidecarResponse response = request(
        "export_segment",
        {{"src", sourcePath}, {"t0", t0}, {"t1", t1}, {"path", outputPath}, {"markers", eventsToJson(events)}},
        timeoutMs);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }
    result.ok = true;
    result.path = response.result.value("path").toString();
    result.nChannels = response.result.value("n_channels").toInt();
    result.nSamples = response.result.value("n_samples").toInt();
    result.markerCount = response.result.value("marker_count").toInt();
    return result;
}

SidecarExportResult SidecarClient::exportSamples(
    double fs, const QStringList& chNames, const std::vector<std::vector<float>>& data,
    double t0, double t1, const QString& outputPath, const QVector<SidecarEvent>& events, int timeoutMs) {
    SidecarExportResult result;
    if (data.empty() || data.front().empty() || data.size() != static_cast<std::size_t>(chNames.size())) {
        result.error = invalidPayload("shape", "invalid export channel count");
        return result;
    }
    const qint64 nSamples = static_cast<qint64>(data.front().size());
    const qint64 byteCount = static_cast<qint64>(data.size()) * nSamples * sizeof(float);
    if (byteCount > kMaxPayloadBytes) {
        result.error = makeError("ipc.invalid_frame", {{"reason", "export payload exceeds limit"}});
        return result;
    }
    QByteArray payload;
    payload.resize(byteCount);
    char* destination = payload.data();
    for (const auto& channel : data) {
        if (static_cast<qint64>(channel.size()) != nSamples) {
            result.error = invalidPayload("shape", "export rows must have equal length");
            return result;
        }
        for (float sample : channel) {
            quint32 bits = 0;
            std::memcpy(&bits, &sample, sizeof(bits));
            qToLittleEndian(bits, reinterpret_cast<uchar*>(destination));
            destination += sizeof(bits);
        }
    }
    const SidecarResponse response = exchange(
        "export_samples",
        {{"fs", fs}, {"ch_names", QJsonArray::fromStringList(chNames)},
         {"shape", QJsonArray{chNames.size(), nSamples}}, {"dtype", "float32-le"}, {"unit", "uV"},
         {"t0", t0}, {"t1", t1}, {"path", outputPath}, {"markers", eventsToJson(events)}},
        payload, false, nullptr, timeoutMs);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }
    result.ok = true;
    result.path = response.result.value("path").toString();
    result.nChannels = response.result.value("n_channels").toInt();
    result.nSamples = response.result.value("n_samples").toInt();
    result.markerCount = response.result.value("marker_count").toInt();
    return result;
}

SidecarSliceResult parseSliceBinaryPayload(const QJsonObject& payload, const QByteArray& bytes) {
    SidecarSliceResult result;
    result.handle = payload.value("handle").toString();
    result.t0 = payload.value("t0").toDouble();
    result.t1 = payload.value("t1").toDouble();
    result.fs = payload.value("fs").toDouble();
    result.unit = payload.value("unit").toString();
    const QJsonArray shape = payload.value("shape").toArray();
    if (shape.size() != 2 || !jsonValueToNonNegativeInt(shape.at(0), &result.nChannels) ||
        !jsonValueToNonNegativeInt(shape.at(1), &result.nSamples)) {
        result.error = invalidPayload("shape", "expected exactly 2 non-negative integers");
        return result;
    }
    const qint64 expectedBytes = static_cast<qint64>(result.nChannels) * result.nSamples * static_cast<qint64>(sizeof(float));
    if (expectedBytes != bytes.size() || payload.value("payload_bytes").toInteger(-1) != expectedBytes ||
        payload.value("dtype").toString() != "float32-le") {
        result.error = makeError("ipc.invalid_frame", {{"reason", "slice payload metadata does not match bytes"}});
        return result;
    }
    result.data.resize(result.nChannels);
    const uchar* source = reinterpret_cast<const uchar*>(bytes.constData());
    for (int channel = 0; channel < result.nChannels; ++channel) {
        result.data[channel].resize(result.nSamples);
        for (int sample = 0; sample < result.nSamples; ++sample) {
            const quint32 bits = qFromLittleEndian<quint32>(source);
            source += sizeof(bits);
            std::memcpy(&result.data[channel][sample], &bits, sizeof(bits));
        }
    }
    result.ok = true;
    return result;
}

SidecarSliceResult parseSlicePayload(const QJsonObject& payload) {
    SidecarSliceResult result;
    result.handle = payload.value("handle").toString();
    result.t0 = payload.value("t0").toDouble();
    result.t1 = payload.value("t1").toDouble();
    result.fs = payload.value("fs").toDouble();
    result.unit = payload.value("unit").toString();
    const QJsonArray shape = payload.value("shape").toArray();
    if (shape.size() != 2 || !jsonValueToNonNegativeInt(shape.at(0), &result.nChannels) ||
        !jsonValueToNonNegativeInt(shape.at(1), &result.nSamples)) {
        result.error = invalidPayload("shape", "expected exactly 2 integers");
        return result;
    }
    const QJsonArray rows = payload.value("data").toArray();
    if (rows.size() != result.nChannels) {
        result.error = invalidPayload(
            "shape", QString("expected %1 rows, got %2").arg(result.nChannels).arg(rows.size()));
        return result;
    }
    for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const QJsonArray values = rows.at(rowIndex).toArray();
        if (values.size() != result.nSamples) {
            result.error = invalidPayload(
                "shape",
                QString("row %1 expected %2 samples, got %3")
                    .arg(rowIndex)
                    .arg(result.nSamples)
                    .arg(values.size()));
            return result;
        }
        std::vector<float> row;
        row.reserve(values.size());
        for (const QJsonValue& value : values) {
            if (!value.isDouble()) {
                result.error = invalidPayload("data", "sample must be a JSON number");
                return result;
            }
            row.push_back(static_cast<float>(value.toDouble()));
        }
        result.data.push_back(std::move(row));
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
