#pragma once

#include "core/app_error.h"
#include "core/version.h"
#include "ipc/sidecar_client.h"

#include <QObject>
#include <QString>
#include <QThread>
#include <QVector>

namespace bf {

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
        const QVector<SidecarEvent>& events);
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
