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
      requestSerial_(std::make_shared<std::atomic<int>>(0)) {}

FileOpenController::~FileOpenController() {
    requestSerial_->store(std::numeric_limits<int>::max());
    const QVector<QThread*> threads = threads_;
    for (QThread* thread : threads) {
        if (thread != nullptr && thread->isRunning()) {
            thread->wait();
        }
    }
}

void FileOpenController::openInitialWindow(const QString& path) {
    const QString repoRoot = repoRoot_;
    const VersionInfo version = version_;
    const std::shared_ptr<std::atomic<int>> requestSerial = requestSerial_;
    const int serial = requestSerial->fetch_add(1) + 1;
    const QPointer<FileOpenController> controller(this);

    auto* thread = QThread::create([controller, requestSerial, repoRoot, version, serial, path]() {
        auto emitFailure = [controller, requestSerial, serial](const AppError& error) {
            if (controller.isNull()) {
                return;
            }
            QMetaObject::invokeMethod(controller.data(), [controller, requestSerial, serial, error]() {
                if (!controller.isNull() && serial == requestSerial->load()) {
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

        const double t1 = std::min(10.0, opened.duration);
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
        QMetaObject::invokeMethod(controller.data(), [controller, requestSerial, serial, result]() {
            if (!controller.isNull() && serial == requestSerial->load()) {
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
