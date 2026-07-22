#pragma once

#include "core/app_error.h"
#include "core/version.h"
#include "ipc/sidecar_client.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace bf {

struct RecordSummary {
    QString handle;
    QJsonObject paths;
    int sourceDroppedChunks = 0;
    int samplesWritten = 0;
    int droppedChunks = 0;
    int droppedEvents = 0;
};

class RecordController final : public QObject {
    Q_OBJECT

public:
    RecordController(QString repoRoot, VersionInfo version, QObject* parent = nullptr);
    ~RecordController() override;

    bool startFromFile(
        const QString& sourcePath,
        double fs,
        const QStringList& channelNames,
        double duration,
        double sourceStart,
        const QString& outputDir,
        const QJsonObject& metadata,
        int chunkSamples = 1024,
        int maxSourceQueueChunks = 8);
    bool appendEvent(double time, const QString& label);
    void stop();
    bool isActive() const;

signals:
    void started(const bf::RecordSummary& summary);
    void progress(const bf::RecordSummary& summary);
    void stopped(const bf::RecordSummary& summary);
    void failed(const bf::AppError& error);

private:
    struct State;
    QString repoRoot_;
    VersionInfo version_;
    std::shared_ptr<State> state_;
    QThread* worker_ = nullptr;
};

}  // namespace bf
