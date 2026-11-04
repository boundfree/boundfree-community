#include "controller/live_ica_controller.h"
#include <QPointer>
#include <QThreadPool>

namespace bf {
LiveIcaController::LiveIcaController(AcquisitionController* acquisition, QObject* parent)
    : QObject(parent), acquisition_(acquisition) {
    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, &LiveIcaController::request);
    connect(acquisition_, &AcquisitionController::rawSignalWindowFailed, this,
        [this](quint64 id, const AppError& error) {
            if (!active_ || id != requestId_) return;
            busy_ = false; stop(); emit failed(error);
        });
    connect(acquisition_, &AcquisitionController::rawSignalWindowReady, this,
        [this](quint64 id, const WaveformSlice& raw) {
            if (!active_ || id != requestId_) return;
            const double end = raw.t1;
            if (latestTime_ < 0) {
                latestTime_ = end; freshness_.restart(); busy_ = false; return;
            }
            if (end > latestTime_ + 1e-9) { latestTime_ = end; freshness_.restart(); haveNewSamples_ = true; }
            if (freshness_.elapsed() > 2500) {
                busy_ = false; stop(); emit failed(makeError("signal.no_samples")); return;
            }
            if (!haveNewSamples_) { busy_ = false; return; }
            const auto revision = revision_;
            const auto model = model_; const auto labels = labels_;
            const QPointer<LiveIcaController> self(this);
            QThreadPool::globalInstance()->start([self, revision, id, model, labels, raw] {
                std::shared_ptr<const LiveIcaFrame> frame; AppError error;
                try { frame = std::make_shared<LiveIcaFrame>(projectLiveIca(*model, labels, raw)); }
                catch (...) { error = makeError("signal.invalid_window"); }
                if (!self) return;
                QMetaObject::invokeMethod(self, [self, revision, id, frame, error, model] {
                    if (!self || !self->active_ || self->requestId_ != id) return;
                    self->busy_ = false;
                    if (self->revision_ != revision) { self->request(); return; }
                    if (!liveIcaModelCompatible(*model, self->acquisition_->signalSourceId()) ||
                        self->acquisition_->snapshot().connection != DeviceConnectionState::Connected ||
                        self->acquisition_->snapshot().acquisition != DeviceAcquisitionState::Streaming) {
                        self->stop(); emit self->failed(makeError("signal.no_samples"));
                    } else if (!error.code.isEmpty()) { self->stop(); emit self->failed(error); }
                    else emit self->frameReady(frame);
                }, Qt::QueuedConnection);
            });
        });
}
void LiveIcaController::start(std::shared_ptr<const SeparationOutput> model, const std::vector<int>& labels) {
    stop();
    if (!model || !liveIcaModelCompatible(*model, acquisition_->signalSourceId()) ||
        acquisition_->snapshot().connection != DeviceConnectionState::Connected ||
        acquisition_->snapshot().acquisition != DeviceAcquisitionState::Streaming) {
        emit failed(makeError("signal.invalid_window")); return;
    }
    model_ = std::move(model); labels_ = labels; active_ = true;
    latestTime_ = -1; haveNewSamples_ = false; freshness_.start();
    emit activeChanged(true); timer_.start(); request();
}
void LiveIcaController::stop() {
    ++revision_; ++requestId_; timer_.stop(); busy_ = false; model_.reset();
    if (active_) { active_ = false; emit activeChanged(false); }
}
void LiveIcaController::setLabels(const std::vector<int>& labels) {
    if (labels_ == labels) return;
    labels_ = labels;
    if (active_) { ++revision_; request(); }
}
void LiveIcaController::request() {
    if (!active_ || busy_) return;
    busy_ = true; ++requestId_;
    const auto origin = model_->manifest.value("source").toObject().value("signal_provenance").toObject();
    acquisition_->requestRawSignalWindow(requestId_, origin.value("stream_id").toString());
}
}
