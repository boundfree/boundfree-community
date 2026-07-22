#include "controller/record_controller.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>

#include <algorithm>
#include <atomic>
#include <utility>

namespace bf {

struct RecordController::State {
    QMutex mutex;
    QWaitCondition hasData;
    QQueue<std::vector<std::vector<float>>> chunks;
    QVector<QPair<double, QString>> pendingEvents;
    std::atomic_bool stopRequested{false};
    std::atomic_bool producerDone{false};
    std::atomic_bool active{false};
    QString handle;
    int maxSourceQueueChunks = 8;
    int sourceDroppedChunks = 0;
};

RecordController::RecordController(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent), repoRoot_(std::move(repoRoot)), version_(std::move(version)), state_(std::make_shared<State>()) {}

RecordController::~RecordController() {
    stop();
    if (worker_ != nullptr && worker_->isRunning()) worker_->wait();
}

bool RecordController::isActive() const {
    return state_->active.load();
}

bool RecordController::startFromFile(
    const QString& sourcePath,
    double fs,
    const QStringList& channelNames,
    double duration,
    double sourceStart,
    const QString& outputDir,
    const QJsonObject& metadata,
    int chunkSamples,
    int maxSourceQueueChunks) {
    if (state_->active.exchange(true) || sourcePath.isEmpty() || fs <= 0.0 || channelNames.isEmpty() ||
        duration <= 0.0 || sourceStart < 0.0 || sourceStart >= duration || outputDir.isEmpty() ||
        chunkSamples <= 0 || maxSourceQueueChunks <= 0) {
        state_->active.store(false);
        return false;
    }
    {
        QMutexLocker lock(&state_->mutex);
        state_->chunks.clear();
        state_->pendingEvents.clear();
        state_->handle.clear();
        state_->sourceDroppedChunks = 0;
        state_->maxSourceQueueChunks = maxSourceQueueChunks;
    }
    state_->stopRequested.store(false);
    state_->producerDone.store(false);
    const QString repoRoot = repoRoot_;
    const VersionInfo version = version_;
    const std::shared_ptr<State> state = state_;
    const QPointer<RecordController> controller(this);

    worker_ = QThread::create([controller, state, repoRoot, version, sourcePath, fs, channelNames, duration, sourceStart, outputDir, metadata, chunkSamples]() {
        auto emitFailure = [controller](const AppError& error) {
            if (!controller.isNull()) QMetaObject::invokeMethod(controller.data(), [controller, error]() {
                if (!controller.isNull()) emit controller->failed(error);
            }, Qt::QueuedConnection);
        };

        SidecarClient writer(repoRoot);
        const SidecarHelloResult writerHello = writer.startAndHello(version, 5000);
        if (!writerHello.ok) {
            emitFailure(writerHello.error);
            state->active.store(false);
            return;
        }
        const SidecarRecordOpenResult opened = writer.recordOpen(fs, channelNames, outputDir, metadata, 64, 15000);
        if (!opened.ok) {
            emitFailure(opened.error);
            state->active.store(false);
            return;
        }
        {
            QMutexLocker lock(&state->mutex);
            state->handle = opened.handle;
        }
        RecordSummary summary;
        summary.handle = opened.handle;
        summary.paths = opened.paths;
        if (!controller.isNull()) QMetaObject::invokeMethod(controller.data(), [controller, summary]() {
            if (!controller.isNull()) emit controller->started(summary);
        }, Qt::QueuedConnection);

        QThread* producer = QThread::create([state, repoRoot, version, sourcePath, fs, duration, sourceStart, chunkSamples, emitFailure]() {
            SidecarClient source(repoRoot);
            const SidecarHelloResult hello = source.startAndHello(version, 5000);
            if (!hello.ok) {
                emitFailure(hello.error);
                state->stopRequested.store(true);
                state->producerDone.store(true);
                state->hasData.wakeAll();
                return;
            }
            const SidecarOpenResult openedSource = source.openFile(sourcePath, 30000);
            if (!openedSource.ok) {
                emitFailure(openedSource.error);
                state->stopRequested.store(true);
                state->producerDone.store(true);
                state->hasData.wakeAll();
                return;
            }
            const double chunkSeconds = chunkSamples / fs;
            QElapsedTimer timer;
            timer.start();
            int chunkIndex = 0;
            for (double t0 = sourceStart; t0 < duration && !state->stopRequested.load(); t0 += chunkSeconds, ++chunkIndex) {
                const qint64 dueMs = static_cast<qint64>(chunkIndex * chunkSeconds * 1000.0);
                while (!state->stopRequested.load() && timer.elapsed() < dueMs) {
                    QThread::msleep(static_cast<unsigned long>(std::min<qint64>(10, dueMs - timer.elapsed())));
                }
                if (state->stopRequested.load()) break;
                const double t1 = std::min(duration, t0 + chunkSeconds);
                const SidecarSliceResult slice = source.getSlice(openedSource.handle, t0, t1, 30000);
                if (!slice.ok) {
                    emitFailure(slice.error);
                    state->stopRequested.store(true);
                    break;
                }
                QMutexLocker lock(&state->mutex);
                if (state->chunks.size() >= state->maxSourceQueueChunks) {
                    ++state->sourceDroppedChunks;
                } else {
                    state->chunks.enqueue(slice.data);
                    state->hasData.wakeOne();
                }
            }
            source.stop();
            state->producerDone.store(true);
            state->hasData.wakeAll();
        });
        producer->start();

        while (true) {
            std::vector<std::vector<float>> chunk;
            QVector<QPair<double, QString>> events;
            {
                QMutexLocker lock(&state->mutex);
                while (state->chunks.isEmpty() && !state->producerDone.load()) state->hasData.wait(&state->mutex, 100);
                if (!state->chunks.isEmpty()) chunk = state->chunks.dequeue();
                events.swap(state->pendingEvents);
                summary.sourceDroppedChunks = state->sourceDroppedChunks;
            }
            for (const auto& event : events) {
                const SidecarRecordResult eventResult = writer.recordEvent(opened.handle, event.first, event.second, 5000);
                if (!eventResult.ok) {
                    emitFailure(eventResult.error);
                    state->stopRequested.store(true);
                }
            }
            if (!chunk.empty()) {
                const SidecarRecordResult write = writer.recordWrite(opened.handle, chunk, 10000);
                if (!write.ok) {
                    emitFailure(write.error);
                    state->stopRequested.store(true);
                } else {
                    summary.samplesWritten = write.samplesWritten;
                    summary.droppedChunks = write.droppedChunks;
                    summary.droppedEvents = write.droppedEvents;
                    if (!controller.isNull()) QMetaObject::invokeMethod(controller.data(), [controller, summary]() {
                        if (!controller.isNull()) emit controller->progress(summary);
                    }, Qt::QueuedConnection);
                }
            }
            if (state->producerDone.load()) {
                QMutexLocker lock(&state->mutex);
                if (state->chunks.isEmpty()) break;
            }
        }
        producer->wait();
        delete producer;

        const SidecarRecordResult closed = writer.recordClose(opened.handle, 30000);
        if (!closed.ok) {
            emitFailure(closed.error);
        } else {
            summary.paths = closed.paths;
            summary.samplesWritten = closed.samplesWritten;
            summary.droppedChunks = closed.droppedChunks;
            summary.droppedEvents = closed.droppedEvents;
            if (!controller.isNull()) QMetaObject::invokeMethod(controller.data(), [controller, summary]() {
                if (!controller.isNull()) emit controller->stopped(summary);
            }, Qt::QueuedConnection);
        }
        writer.stop();
        {
            QMutexLocker lock(&state->mutex);
            state->handle.clear();
            state->pendingEvents.clear();
        }
        state->active.store(false);
    });
    connect(worker_, &QThread::finished, this, [this]() { worker_ = nullptr; });
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
    worker_->start();
    return true;
}

bool RecordController::appendEvent(double time, const QString& label) {
    QMutexLocker lock(&state_->mutex);
    if (!state_->active.load() || state_->handle.isEmpty()) return false;
    state_->pendingEvents.push_back({time, label});
    state_->hasData.wakeOne();
    return true;
}

void RecordController::stop() {
    state_->stopRequested.store(true);
    state_->hasData.wakeAll();
}

}  // namespace bf
