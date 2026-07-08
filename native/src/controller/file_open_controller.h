#pragma once

#include "core/app_error.h"
#include "core/version.h"
#include "ipc/sidecar_client.h"
#include "render/waveform_slice.h"

#include <QObject>
#include <QString>
#include <atomic>

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

    void openInitialWindow(const QString& path);

signals:
    void loaded(const bf::FileOpenResult& result);
    void failed(const bf::AppError& error);

private:
    QString repoRoot_;
    VersionInfo version_;
    std::atomic<int> requestSerial_{0};
};

}  // namespace bf
