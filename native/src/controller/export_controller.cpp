#include "controller/export_controller.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <cmath>
#include <utility>

namespace bf {

ExportController::ExportController(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent), repoRoot_(std::move(repoRoot)), version_(std::move(version)) {}

ExportController::~ExportController() {
    if (worker_ != nullptr && worker_->isRunning()) {
        worker_->wait();
    }
}

bool ExportController::exportAsync(
    const QString& sourcePath,
    double t0,
    double t1,
    double duration,
    const QString& outputPath,
    const QVector<SidecarEvent>& events) {
    if (worker_ != nullptr || sourcePath.isEmpty() || outputPath.isEmpty() || !std::isfinite(t0) ||
        !std::isfinite(t1) || t0 < 0.0 || t1 <= t0 || t1 > duration) {
        return false;
    }
    const QString suffix = QFileInfo(outputPath).suffix().toLower();
    if (suffix != "edf" && suffix != "h5" && suffix != "hdf5" && suffix != "csv") {
        return false;
    }
    const QString repoRoot = repoRoot_;
    const VersionInfo version = version_;
    const QPointer<ExportController> controller(this);
    worker_ = QThread::create([controller, repoRoot, version, sourcePath, t0, t1, outputPath, events]() {
        SidecarClient sidecar(repoRoot);
        const SidecarHelloResult hello = sidecar.startAndHello(version, 5000);
        if (!hello.ok) {
            if (!controller.isNull()) QMetaObject::invokeMethod(controller.data(), [controller, hello]() {
                if (!controller.isNull()) emit controller->failed(hello.error);
            }, Qt::QueuedConnection);
            return;
        }
        const SidecarExportResult result = sidecar.exportSegment(sourcePath, t0, t1, outputPath, events, 60000);
        sidecar.stop();
        if (controller.isNull()) return;
        QMetaObject::invokeMethod(controller.data(), [controller, result]() {
            if (controller.isNull()) return;
            if (result.ok) emit controller->completed(result);
            else emit controller->failed(result.error);
        }, Qt::QueuedConnection);
    });
    connect(worker_, &QThread::finished, this, [this]() { worker_ = nullptr; });
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
    worker_->start();
    return true;
}

}  // namespace bf
