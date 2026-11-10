#pragma once

#include "core/app_error.h"
#include "core/version.h"
#include "ipc/sidecar_client.h"
#include "render/processing_view_state.h"
#include "render/waveform_slice.h"

#include <QObject>
#include <QString>
#include <QThread>
#include <QVector>
#include <optional>

namespace bf {

struct ExportProcessingOptions {
    ProcessingViewState state;
    // Include the displayed filter context so an export of that window matches its samples.
    double contextStart = -1.0;
    double contextEnd = 0.0;
    QString streamKey;
    bool causal = false;
    bool applyProcessing = true;
};

class ExportController final : public QObject {
    Q_OBJECT

public:
    ExportController(QString repoRoot, VersionInfo version, QObject* parent = nullptr);
    ~ExportController() override;

    bool exportAsync(
        const QString& sourcePath,
        double t0,
        double t1,
        double duration,
        const QString& outputPath,
        const QVector<SidecarEvent>& events,
        const std::optional<ExportProcessingOptions>& processing = std::nullopt);
    bool exportSignalAsync(const WaveformSlice&, const QString&, const QVector<SidecarEvent>&, const QJsonObject& provenance = {});
    bool isBusy() const { return worker_ != nullptr; }

signals:
    void completed(const bf::SidecarExportResult& result);
    void failed(const bf::AppError& error);

private:
    QString repoRoot_;
    VersionInfo version_;
    QThread* worker_ = nullptr;
};

}  // namespace bf
