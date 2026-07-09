#include "controller/file_open_controller.h"

#include "render/waveform_slice.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <algorithm>
#include <limits>
#include <utility>

namespace bf {

FileOpenController::FileOpenController(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent),
      repoRoot_(std::move(repoRoot)),
      version_(std::move(version)),
      requestGeneration_(std::make_shared<RequestGeneration>()) {}

FileOpenController::~FileOpenController() {
    requestGeneration_->invalidate();
    const QVector<QThread*> threads = threads_;
    for (QThread* thread : threads) {
        if (thread != nullptr && thread->isRunning()) {
            thread->wait();
        }
    }
}

void FileOpenController::openInitialWindow(const QString& path, double windowSeconds) {
    const QString repoRoot = repoRoot_;
    const VersionInfo version = version_;
    const std::shared_ptr<RequestGeneration> requestGeneration = requestGeneration_;
    const int generation = requestGeneration->next();
    const double requestedWindowSeconds = std::clamp(windowSeconds, 1.0, 120.0);
    const QPointer<FileOpenController> controller(this);

    auto* thread = QThread::create([controller, requestGeneration, repoRoot, version, generation, path, requestedWindowSeconds]() {
        auto emitFailure = [controller, requestGeneration, generation](const AppError& error) {
            if (controller.isNull()) {
                return;
            }
            QMetaObject::invokeMethod(controller.data(), [controller, requestGeneration, generation, error]() {
                if (!controller.isNull() && requestGeneration->isCurrent(generation)) {
                    emit controller->failed(error);
                }
            }, Qt::QueuedConnection);
        };

        SidecarClient sidecar(repoRoot);
        const SidecarHelloResult hello = sidecar.startAndHello(version);
        if (!hello.ok) {
            sidecar.stop();
            emitFailure(hello.error);
            return;
        }

        SidecarOpenResult opened = sidecar.openFile(path, 30000);
        if (!opened.ok) {
            sidecar.stop();
            emitFailure(opened.error);
            return;
        }

        const double t1 = std::min(requestedWindowSeconds, opened.duration);
        SidecarSliceResult slice = sidecar.getSlice(opened.handle, 0.0, t1, 30000);
        sidecar.stop();
        if (!slice.ok) {
            emitFailure(slice.error);
            return;
        }

        FileOpenResult result{opened, slice, waveformSliceFromSidecar(opened, slice)};
        if (controller.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(controller.data(), [controller, requestGeneration, generation, result]() {
            if (!controller.isNull() && requestGeneration->isCurrent(generation)) {
                emit controller->loaded(result);
            }
        }, Qt::QueuedConnection);
    });
    threads_.append(thread);
    connect(thread, &QThread::finished, this, [this, thread]() {
        threads_.removeAll(thread);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

}  // namespace bf
