#pragma once

#include "controller/request_generation.h"
#include "core/app_error.h"
#include "core/version.h"
#include "ipc/sidecar_client.h"
#include "render/waveform_slice.h"

#include <QObject>
#include <QString>
#include <QThread>
#include <memory>

namespace bf {

struct FileOpenResult {
    SidecarOpenResult session;
    SidecarSliceResult initialSlice;
    WaveformSlice waveform;
};

class FileOpenController : public QObject {
    Q_OBJECT

public:
    FileOpenController(QString repoRoot, VersionInfo version, QObject* parent = nullptr);
    ~FileOpenController() override;

    void closeSession();
    void openInitialWindow(const QString& path, double windowSeconds = 10.0, const QString& captureStream = {});
    void openWindow(const QString& path, double t0, double windowSeconds);

signals:
    void loaded(const bf::FileOpenResult& result);
    void failed(const bf::AppError& error);

private:
    struct WorkerState;
    void enqueueWindow(const QString& path, double t0, double windowSeconds, bool reopen, const QString& captureStream = {});

    QString repoRoot_;
    VersionInfo version_;
    std::shared_ptr<RequestGeneration> requestGeneration_;
    std::shared_ptr<WorkerState> workerState_;
    QThread* worker_ = nullptr;
};

}  // namespace bf
