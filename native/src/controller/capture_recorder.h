#pragma once

#include "acquisition/buffer/signal_hub.h"
#include "core/app_error.h"
#include "core/version.h"

#include <QObject>
#include <QString>
#include <memory>

class QThread;

namespace bf {

enum class CaptureRecordState { Off, Opening, Recording, Finalizing, Saved, Partial, Failed };

struct CaptureRecordSnapshot {
    CaptureRecordState state = CaptureRecordState::Off;
    QString path;
    quint64 acceptedSamples = 0;
    quint64 writtenSamples = 0;
    quint64 flushedSamples = 0;
    AppError error;
};

// One recorder owns a dedicated sidecar and bounded SignalHub consumer.
// The public methods run on the GUI thread; no sample payloads enter Qt's
// event queue. stop() closes the consumer before draining accepted blocks,
// including blocks queued while the sidecar is opening the capture.
class CaptureRecorder final : public QObject {
    Q_OBJECT
public:
    CaptureRecorder(QString repoRoot, VersionInfo version, QObject* parent = nullptr);
    ~CaptureRecorder() override;

    bool start(const QString& outputDirectory,
               const acquisition::DeviceDescriptor& device,
               const acquisition::CaptureDescriptor& capture,
               std::shared_ptr<acquisition::SignalHub> hub);
    void stop(bool interrupted = false);
    void clearResult();
    bool isActive() const;
    CaptureRecordSnapshot snapshot() const;

signals:
    void stateChanged();
    void finished();

private:
    struct State;
    QString repoRoot_;
    VersionInfo version_;
    std::shared_ptr<State> state_;
    QThread* worker_ = nullptr;
};

} // namespace bf
