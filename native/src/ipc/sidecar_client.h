#pragma once

#include "core/app_error.h"
#include "core/version.h"

#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <QByteArray>
#include <vector>
#include <functional>

namespace bf {

struct SidecarHelloResult {
    bool ok = false;
    QString productVersion;
    int ipcProtocolVersion = 0;
    AppError error;
    QStringList analysisCapabilities;
};

struct SidecarResponse {
    bool ok = false;
    QJsonObject result;
    AppError error;
};

struct SidecarEvent {
    double time = 0.0;
    QString label;
};

struct SidecarOpenResult {
    bool ok = false;
    QString handle;
    QString path;
    double fs = 0.0;
    int nChannels = 0;
    int nSamples = 0;
    double duration = 0.0;
    QString format;
    QStringList chNames;
    QVector<SidecarEvent> events;
    AppError error;
};

struct SidecarSliceResult {
    bool ok = false;
    QString handle;
    double t0 = 0.0;
    double t1 = 0.0;
    double fs = 0.0;
    int nChannels = 0;
    int nSamples = 0;
    QString unit;
    std::vector<std::vector<float>> data;
    AppError error;
};

SidecarSliceResult parseSlicePayload(const QJsonObject& payload);
SidecarSliceResult parseSliceBinaryPayload(const QJsonObject& payload, const QByteArray& bytes);

struct SidecarRecordOpenResult {
    bool ok = false;
    QString handle;
    QJsonObject paths;
    AppError error;
};

struct SidecarRecordResult {
    bool ok = false;
    bool accepted = false;
    int backlogChunks = 0;
    int samplesWritten = 0;
    int droppedChunks = 0;
    int droppedEvents = 0;
    QJsonObject paths;
    AppError error;
};

struct SidecarExportResult {
    bool ok = false;
    QString path;
    int nChannels = 0;
    int nSamples = 0;
    int markerCount = 0;
    AppError error;
};

class SidecarClient {
public:
    explicit SidecarClient(QString repoRoot);
    ~SidecarClient();

    SidecarHelloResult startAndHello(const VersionInfo& version, int timeoutMs = 3000);
    SidecarResponse request(const QString& method, const QJsonObject& params, int timeoutMs = 3000);
    // Explicit binary methods negotiate their own dtype; existing slices remain float32.
    SidecarResponse requestBinary(const QString& method, const QJsonObject& params,
                                  const QByteArray& outgoing, QByteArray* incoming,
                                  int timeoutMs = 30000);
    void setCancellation(std::function<bool()> cancelled) { cancelled_ = std::move(cancelled); }
    SidecarOpenResult openFile(const QString& path, int timeoutMs = 3000);
    SidecarResponse exportMarkersCsv(const QString& path, const QVector<SidecarEvent>& events, int timeoutMs = 3000);
    SidecarSliceResult getSlice(const QString& handle, double t0, double t1, int timeoutMs = 3000);
    SidecarRecordOpenResult recordOpen(
        double fs,
        const QStringList& chNames,
        const QString& outputDir,
        const QJsonObject& metadata,
        int maxQueueChunks = 256,
        int timeoutMs = 10000);
    SidecarRecordResult recordWrite(
        const QString& handle,
        const std::vector<std::vector<float>>& data,
        int timeoutMs = 3000);
    SidecarRecordResult recordEvent(const QString& handle, double time, const QString& label, int timeoutMs = 3000);
    SidecarRecordResult recordClose(const QString& handle, int timeoutMs = 15000);
    SidecarExportResult exportSegment(
        const QString& sourcePath,
        double t0,
        double t1,
        const QString& outputPath,
        const QVector<SidecarEvent>& events,
        int timeoutMs = 30000);
    SidecarExportResult exportSamples(
        double fs,
        const QStringList& chNames,
        const std::vector<std::vector<float>>& data,
        double t0,
        double t1,
        const QString& outputPath,
        const QVector<SidecarEvent>& events,
        int timeoutMs = 30000);
    void stop();

private:
    bool writeFrame(quint8 frameType, const QByteArray& payload, int timeoutMs);
    bool readFrame(quint8* frameType, QByteArray* payload, int timeoutMs);
    SidecarResponse exchange(
        const QString& method,
        const QJsonObject& params,
        const QByteArray& outgoingPayload,
        bool expectBinaryPayload,
        QByteArray* incomingPayload,
        int timeoutMs);

    QString repoRoot_;
    QProcess process_;
    std::function<bool()> cancelled_;
};

}  // namespace bf
