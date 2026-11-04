#include "controller/file_open_controller.h"

#include "render/waveform_slice.h"

#include <QMetaObject>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace bf {

struct FileOpenController::WorkerState {
    struct Request {
        QString path;
        double t0;
        double t1;
        int generation;
        quint64 revision;
        QString captureStream;
    };

    std::mutex mutex;
    std::condition_variable ready;
    std::optional<Request> pending;
    bool stopping = false;
    quint64 revision = 0;
    QString captureStream;
};

FileOpenController::FileOpenController(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent),
      repoRoot_(std::move(repoRoot)),
      version_(std::move(version)),
      requestGeneration_(std::make_shared<RequestGeneration>()),
      workerState_(std::make_shared<WorkerState>()) {
    const auto state = workerState_;
    const auto generations = requestGeneration_;
    // QProcess belongs to this worker for its entire lifetime. Only the
    // request mailbox and generation counter are shared with the UI thread.
    worker_ = QThread::create([this, state, generations, root = repoRoot_, version = version_]() {
        SidecarClient sidecar(root);
        SidecarOpenResult opened;
        QString openedPath;
        quint64 openedRevision = 0;
        bool connected = false;
        const auto resetSession = [&]() {
            sidecar.stop();
            opened = {};
            openedPath.clear();
            connected = false;
        };
        const auto fail = [&](int generation, AppError error) {
            resetSession();
            QMetaObject::invokeMethod(this, [this, generations, generation, error]() {
                if (generations->isCurrent(generation)) emit failed(error);
            }, Qt::QueuedConnection);
        };

        while (true) {
            WorkerState::Request request;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->ready.wait(lock, [&]() { return state->stopping || state->pending.has_value(); });
                if (state->stopping) break;
                request = std::move(*state->pending);
                state->pending.reset();
            }
            if (!generations->isCurrent(request.generation)) continue;
            if (request.path.isEmpty()) {
                resetSession();
                continue;
            }
            if (!connected) {
                // A cold frozen sidecar may need as long as the initial file
                // read to start; keep the GUI responsive while the worker waits.
                const SidecarHelloResult hello = sidecar.startAndHello(version, 30000);
                if (!hello.ok) {
                    fail(request.generation, hello.error);
                    continue;
                }
                connected = true;
            }
            if (!generations->isCurrent(request.generation)) continue;
            if (opened.ok && (openedRevision != request.revision || openedPath != request.path)) {
                const SidecarResponse closed = sidecar.request("close", {{"handle", opened.handle}});
                if (!closed.ok) {
                    fail(request.generation, closed.error);
                    continue;
                }
                opened = {};
                openedPath.clear();
            }
            if (!generations->isCurrent(request.generation)) continue;
            if (!opened.ok) {
                opened = sidecar.openFile(request.path, 30000, request.captureStream);
                if (!opened.ok) {
                    fail(request.generation, opened.error);
                    continue;
                }
                openedPath = request.path;
                openedRevision = request.revision;
            }
            if (!generations->isCurrent(request.generation)) continue;

            const double sliceT0 = std::min(request.t0, opened.duration);
            const double sliceT1 = std::min(request.t1, opened.duration);
            if (sliceT1 <= sliceT0) {
                fail(request.generation, makeError("reader.slice_out_of_range", {
                    {"handle", opened.handle}, {"t0", sliceT0},
                    {"t1", sliceT1}, {"duration", opened.duration},
                }));
                continue;
            }
            SidecarSliceResult slice = sidecar.getSlice(opened.handle, sliceT0, sliceT1, 30000);
            if (!slice.ok) {
                fail(request.generation, slice.error);
                continue;
            }
            if (!generations->isCurrent(request.generation)) continue;
            FileOpenResult result{opened, slice, waveformSliceFromSidecar(opened, slice)};
            QMetaObject::invokeMethod(this, [this, generations, generation = request.generation, result = std::move(result)]() {
                if (generations->isCurrent(generation)) emit loaded(result);
            }, Qt::QueuedConnection);
        }
        // Destruction joins this thread before QObject teardown; queued results
        // are invalidated and removed by QObject, and no QProcess crosses threads.
        if (connected && opened.ok) sidecar.request("close", {{"handle", opened.handle}});
        sidecar.stop();
    });
    worker_->start();
}

FileOpenController::~FileOpenController() {
    requestGeneration_->invalidate();
    {
        std::lock_guard<std::mutex> lock(workerState_->mutex);
        workerState_->stopping = true;
        workerState_->pending.reset();
    }
    workerState_->ready.notify_one();
    // An in-flight synchronous IPC operation finishes under its existing
    // timeout. Never terminate QProcess from the UI thread.
    worker_->wait();
    delete worker_;
}

void FileOpenController::closeSession() {
    const int generation = requestGeneration_->next();
    {
        std::lock_guard<std::mutex> lock(workerState_->mutex);
        ++workerState_->revision;
        workerState_->captureStream.clear();
        workerState_->pending = WorkerState::Request{{}, 0, 0, generation, workerState_->revision, {}};
    }
    workerState_->ready.notify_one();
}

void FileOpenController::openInitialWindow(const QString& path, double windowSeconds, const QString& captureStream) {
    enqueueWindow(path, 0.0, windowSeconds, true, captureStream);
}

void FileOpenController::openWindow(const QString& path, double t0, double windowSeconds) {
    enqueueWindow(path, t0, windowSeconds, false);
}

void FileOpenController::enqueueWindow(const QString& path, double t0, double windowSeconds, bool reopen, const QString& captureStream) {
    const int generation = requestGeneration_->next();
    const double requestedT0 = std::max(0.0, t0);
    // Display windows remain <=120s; allow at most 8s of bounded lookahead.
    const double requestedWindowSeconds = std::min(std::max(windowSeconds, 0.0), 128.0);
    {
        std::lock_guard<std::mutex> lock(workerState_->mutex);
        // An explicit reopen changes the revision even when a newer seek
        // replaces its mailbox entry or supersedes an in-flight operation.
        if (reopen) { ++workerState_->revision; workerState_->captureStream = captureStream; }
        workerState_->pending = WorkerState::Request{
            path, requestedT0, requestedT0 + requestedWindowSeconds, generation, workerState_->revision, workerState_->captureStream,
        };
    }
    workerState_->ready.notify_one();
}

}  // namespace bf
