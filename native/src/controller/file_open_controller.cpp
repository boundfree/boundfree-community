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
    openWindow(path, 0.0, windowSeconds);
}

void FileOpenController::openWindow(const QString& path, double t0, double windowSeconds) {
    const QString repoRoot = repoRoot_;
    const VersionInfo version = version_;
    const std::shared_ptr<RequestGeneration> requestGeneration = requestGeneration_;
    const int generation = requestGeneration->next();
    const double requestedT0 = std::max(0.0, t0);
    const double requestedWindowSeconds = std::min(std::max(windowSeconds, 0.0), 120.0);
    const double requestedT1 = requestedT0 + requestedWindowSeconds;
    const QPointer<FileOpenController> controller(this);

    auto* thread = QThread::create([controller, requestGeneration, repoRoot, version, generation, path, requestedT0, requestedT1]() {
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

        const double sliceT0 = std::min(requestedT0, opened.duration);
        const double sliceT1 = std::min(requestedT1, opened.duration);
        if (sliceT1 <= sliceT0) {
            sidecar.stop();
            emitFailure(makeError("reader.slice_out_of_range", {
                {"handle", opened.handle},
                {"t0", sliceT0},
                {"t1", sliceT1},
                {"duration", opened.duration},
            }));
            return;
        }

        SidecarSliceResult slice = sidecar.getSlice(opened.handle, sliceT0, sliceT1, 30000);
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
