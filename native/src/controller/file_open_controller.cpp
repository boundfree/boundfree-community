#include "controller/file_open_controller.h"

#include "render/waveform_slice.h"

#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <utility>

namespace bf {

FileOpenController::FileOpenController(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent), repoRoot_(std::move(repoRoot)), version_(std::move(version)) {}

void FileOpenController::openInitialWindow(const QString& path) {
    const int serial = ++requestSerial_;
    auto* thread = QThread::create([this, serial, path]() {
        auto emitFailure = [this, serial](const AppError& error) {
            QMetaObject::invokeMethod(this, [this, serial, error]() {
                if (serial == requestSerial_.load()) {
                    emit failed(error);
                }
            }, Qt::QueuedConnection);
        };

        SidecarClient sidecar(repoRoot_);
        const SidecarHelloResult hello = sidecar.startAndHello(version_);
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
        QMetaObject::invokeMethod(this, [this, serial, result]() {
            if (serial == requestSerial_.load()) {
                emit loaded(result);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

}  // namespace bf
