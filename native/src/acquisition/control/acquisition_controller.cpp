#include "acquisition/control/acquisition_controller.h"
#include "acquisition/control/acquisition_view.h"
#include "acquisition/control/acquisition_diagnostic_log.h"
#include "acquisition/sources/acquisition_tcp_source.h"
#include "acquisition/sources/acquisition_wifi_source.h"

#include "acquisition/adapters/simulator.h"
#include "acquisition/session/acquisition_session.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QFileInfo>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QMetaObject>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <utility>

namespace bf {
namespace {
using namespace acquisition;
using Clock = std::chrono::steady_clock;

enum class Command { None, Connect, Disconnect, Configure, Start, Stop, Exit };

AppError appError(const DeviceError& error) {
    QJsonObject params;
    for (const auto& entry : error.params) params.insert(QString::fromStdString(entry.first), QString::fromStdString(entry.second));
    return makeError(QString::fromStdString(error.code), params, error.retryable);
}
std::vector<SimStream> simulatorConfig(int rate, bool icaMix) {
    if (icaMix) return {icaSimulatorStream(rate)};
    SimStream stream;
    stream.name = "Simulated EEG";
    stream.rate = {static_cast<std::uint32_t>(rate), 1};
    stream.dtype = DType::Int24;
    stream.samples_per_step = static_cast<std::uint32_t>(rate / 50);
    for (int i = 0; i < 4; ++i) {
        SimChannel channel;
        channel.descriptor.channel_id = "sim-" + std::to_string(i + 1);
        channel.descriptor.name = "CH " + std::to_string(i + 1);
        channel.descriptor.signal_type = "eeg";
        channel.descriptor.unit = Unit::Microvolt;
        channel.descriptor.scale = 0.1;
        channel.descriptor.valid_adc_bits = 24;
        channel.amplitude = 300.0 + i * 50.0;
        channel.frequency_hz = 2.0 + i * 1.5;
        stream.channels.push_back(channel);
    }
    return {stream};
}
} // namespace

struct AcquisitionController::State {
    mutable std::mutex mutex;
    std::condition_variable wake;
    AcquisitionViewSnapshot view;
    Command command = Command::None;
    int requestedRate = 250;
    bool requestedIcaMix = false;
    Generation generation = 0;
    std::shared_ptr<SignalHub> hub;
    std::shared_ptr<SignalConsumer> history;
    DeviceDescriptor device;
    std::optional<CaptureDescriptor> capture;
    bool shuttingDown = false;
    bool shutdownFailed = false;
};

AcquisitionController::AcquisitionController(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent), state_(std::make_shared<State>()) {
    tcpSource_ = new AcquisitionTcpSource(version, this);
    wifiSource_ = new AcquisitionWifiSource(this);
    recorder_ = new CaptureRecorder(std::move(repoRoot), std::move(version), this);
    auto* processingTimer=new QTimer(this);processingTimer->setInterval(200);
    connect(processingTimer,&QTimer::timeout,this,[this]{
        if(signalProcessingEnabled_ && snapshot().acquisition==DeviceAcquisitionState::Streaming)
            requestSignalWindow(lastSignalStream_,lastSignalSettings_,lastSignalCausal_,lastSignalSeconds_);
    });processingTimer->start();
    const auto checkShutdown = [this] {
        const auto view = snapshot();
        if ((view.acquisition == DeviceAcquisitionState::Idle || view.acquisition == DeviceAcquisitionState::Faulted)
                && recorder_->isActive()) recorder_->stop(view.acquisition == DeviceAcquisitionState::Faulted);
        bool finish = false;
        bool success = true;
        { std::lock_guard<std::mutex> lock(state_->mutex);
          if (state_->shuttingDown && (view.connection == DeviceConnectionState::Faulted ||
              view.acquisition == DeviceAcquisitionState::Faulted)) state_->shutdownFailed = true;
          if (state_->shuttingDown && activeSource_ && !sourceStopPending_ && !view.error.code.isEmpty() &&
              (view.acquisition == DeviceAcquisitionState::Starting || view.acquisition == DeviceAcquisitionState::Streaming)) {
              state_->shuttingDown = false; finish = true; success = false;
          } else if (state_->shuttingDown && !recorder_->isActive() && !view.configurationInProgress &&
              view.connection != DeviceConnectionState::Connecting && (view.acquisition == DeviceAcquisitionState::Idle || view.acquisition == DeviceAcquisitionState::Faulted)) {
              state_->shuttingDown = false; finish = true; success = !state_->shutdownFailed;
          } }
        if (finish) emit shutdownFinished(success && view.recording.state != CaptureRecordState::Failed);
    };
    connect(this, &AcquisitionController::stateChanged, this, checkShutdown);
    connect(this, &AcquisitionController::stateChanged, this, [this] {
        const auto path = snapshot().diagnosticLogPath;
        if (!path.isEmpty()) lastDiagnosticLogPath_ = path;
    });
    for (auto* source : {static_cast<AcquisitionSource*>(tcpSource_), static_cast<AcquisitionSource*>(wifiSource_)}) {
        connect(source, &AcquisitionSource::stateChanged, this, [this, source] {
            if (activeSource_ != source) return;
            const auto view = source->snapshot();
            if (sourceOpenPending_ && view.connectionRevision == sourceConnectionRevision_) return;
            if (sourceClosePending_ && view.disconnectionCompletion >= sourceDisconnectionCompletion_)
                sourceClosePending_ = false;
            if (view.connection != DeviceConnectionState::Disconnected) sourceOpenPending_ = false;
            if (sourceStartPending_ && (view.acquisition != DeviceAcquisitionState::Idle ||
                view.acquisitionCompletion != sourceStartCompletion_)) sourceStartPending_ = false;
            if (sourceStopPending_ && (view.acquisitionCompletion != sourceStopCompletion_ ||
                view.connection == DeviceConnectionState::Faulted || view.connection == DeviceConnectionState::Disconnected))
                sourceStopPending_ = false;
            // The GUI may receive only the terminal snapshot of a busy -> done
            // transition. Rate/revision/error equality cannot identify completion.
            if (sourceConfigurePending_ && (view.configCompletion != sourceConfigStartCompletion_ ||
                view.connection == DeviceConnectionState::Faulted || view.connection == DeviceConnectionState::Disconnected))
                sourceConfigurePending_ = false;
            emit stateChanged();
        });
    }
    connect(recorder_, &CaptureRecorder::stateChanged, this, [this] { emit stateChanged(); });
    connect(recorder_, &CaptureRecorder::finished, this, checkShutdown);
    const auto state = state_;
    const QPointer<AcquisitionController> owner(this);
    worker_ = QThread::create([state, owner] {
        auto notify = [owner] {
            if (owner) QMetaObject::invokeMethod(owner.data(), [owner] { if (owner) emit owner->stateChanged(); }, Qt::QueuedConnection);
        };
        MemoryBudget budget(256 * 1024 * 1024);
        std::unique_ptr<InProcessSimulator> simulator;
        std::unique_ptr<AcquisitionSession> session;
        std::shared_ptr<SignalHub> hub;
        std::shared_ptr<SignalConsumer> history;
        std::optional<CaptureDescriptor> capture;
        bool simulatorIcaMix = false;
        AcquisitionDiagnosticLog diagnostics;
        auto next = Clock::now();
        auto nextView = next;
        const auto fail = [&](const DeviceError& error) {
            diagnostics.flushStatistics();
            diagnostics.write("error", {{"code", QString::fromStdString(error.code)}, {"params", appError(error).params}});
            if (hub) hub->close();
            { std::lock_guard<std::mutex> lock(state->mutex);
              state->view.diagnosticLogPath = diagnostics.path();
              state->view.error = appError(error); state->view.acquisition = DeviceAcquisitionState::Faulted;
              if (state->view.connection == DeviceConnectionState::Connecting)
                  state->view.connection = DeviceConnectionState::Faulted;
              state->hub.reset(); state->history.reset(); state->capture.reset(); }
            notify();
        };
        while (true) {
            Command command;
            int rate;
            bool icaMix;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                if (capture && simulator) state->wake.wait_until(lock, std::min(next, nextView), [&] { return state->command != Command::None; });
                else state->wake.wait(lock, [&] { return state->command != Command::None; });
                command = state->command; state->command = Command::None; rate = state->requestedRate;
                icaMix = state->requestedIcaMix;
            }
            if (command == Command::Exit) {
                diagnostics.flushStatistics(); diagnostics.write("shutdown"); diagnostics.close();
                if (capture && simulator) simulator->stop(capture->capture_id);
                if (hub) hub->close();
                break;
            }
            if (command == Command::Connect || command == Command::Configure) {
                if (command == Command::Connect) diagnostics.open("simulator.in_process", true, {{"sample_rate", rate}, {"signal_profile", icaMix ? "ica_mix_v1" : "periodic"}});
                else { diagnostics.flushStatistics(); diagnostics.write("configuration_requested", {{"sample_rate", rate}}); }
                try {
                    simulatorIcaMix = icaMix;
                    simulator = std::make_unique<InProcessSimulator>(simulatorConfig(rate, icaMix), budget);
                    const auto device = simulator->deviceDescriptor();
                    Generation generation;
                    { std::lock_guard<std::mutex> lock(state->mutex); generation = ++state->generation; }
                    session = std::make_unique<AcquisitionSession>(device, SessionId{newIdentity()}, generation);
                    { std::lock_guard<std::mutex> lock(state->mutex);
                      state->view.diagnosticLogPath = diagnostics.path();
                      state->device = device; state->view.deviceName = QString::fromStdString(device.model) + (icaMix ? " — ICA mix" : "");
                      state->view.simulated = true; state->view.canConfigure = true; state->view.sampleRate = rate;
                      state->view.exactRate = {static_cast<std::uint32_t>(rate), 1};
                      state->view.connection = DeviceConnectionState::Connected;
                      state->view.acquisition = DeviceAcquisitionState::Idle; state->view.error = {}; }
                    diagnostics.write(command == Command::Connect ? "connected" : "configured", {{"sample_rate", rate}});
                    notify();
                } catch (...) { fail(DeviceError{"acquisition.invalid_configuration", {}, {}, false, "configuration"}); }
            } else if (command == Command::Disconnect) {
                diagnostics.close();
                if (hub) hub->close();
                simulator.reset(); session.reset(); capture.reset(); hub.reset(); history.reset();
                { std::lock_guard<std::mutex> lock(state->mutex); ++state->generation;
                  state->hub.reset(); state->history.reset(); state->capture.reset(); state->view.connection = DeviceConnectionState::Disconnected;
                  state->view.acquisition = DeviceAcquisitionState::Idle; state->view.simulated = false; state->view.canConfigure = false; }
                notify();
            } else if (command == Command::Start && simulator && session) {
                diagnostics.flushStatistics(); diagnostics.write("stream_start_requested");
                auto prepared = session->prepare(simulator->descriptors());
                if (!prepared) { fail(*prepared.error); continue; }
                capture = *prepared.value;
                if (simulatorIcaMix) capture->metadata = {{"simulator_signal_profile", "ica_mix_v1"},
                    {"simulator_generator", "boundfree.ica-mix.v1"}, {"simulator_channel_roles", "1,1,1,2,3"},
                    {"simulator_mixing", "1,0.5,0.3;0.3,1.2,0.1;1,-0.2,1.1"}};
                auto error = simulator->prepare(*capture);
                if (!error) error = session->start(capture->capture_id);
                if (!error) error = simulator->start(capture->capture_id);
                if (error) { session->stop(capture->capture_id); capture.reset(); fail(*error); continue; }
                try {
                    hub = std::make_shared<SignalHub>(*capture, budget);
                    auto consumer = hub->addConsumer(ConsumerKind::History, {128 * 1024 * 1024, 1500});
                    if (!consumer) { simulator->stop(capture->capture_id); session->stop(capture->capture_id);
                        capture.reset(); fail(*consumer.error); continue; }
                    history = *consumer.value;
                    { std::lock_guard<std::mutex> lock(state->mutex);
                      state->hub = hub; state->history = history; state->capture = capture;
                      state->view.totalChannels = 0;
                      for (const auto& stream : capture->streams) state->view.totalChannels += stream.channels.size();
                      state->view.samples = state->view.missingSamples = state->view.unknownGaps = 0;
                      state->view.traces.clear(); state->view.error = {}; }
                    diagnostics.write("stream_started", {{"capture_id", QString::fromStdString(capture->capture_id.value)},
                        {"streams", int(capture->streams.size())}, {"sample_rate", rate}});
                    next = Clock::now(); nextView = next;
                } catch (...) { simulator->stop(capture->capture_id); session->stop(capture->capture_id);
                    capture.reset(); fail(DeviceError{"acquisition.invalid_capture", {}, {}, false, "prepare"}); }
            } else if (command == Command::Stop) {
                diagnostics.flushStatistics(); diagnostics.write("stream_stopped");
                if (capture && simulator) simulator->stop(capture->capture_id);
                if (capture && session) session->stop(capture->capture_id);
                if (hub) hub->close();
                capture.reset(); hub.reset(); history.reset();
                { std::lock_guard<std::mutex> lock(state->mutex);
                  state->hub.reset(); state->history.reset(); state->capture.reset(); state->view.acquisition = DeviceAcquisitionState::Idle; }
                notify();
            }
            if (!capture || !simulator || !hub) continue;
            const auto now = Clock::now();
            bool published = false;
            if (now >= next) {
                next = now + std::chrono::milliseconds(20);
                auto step = simulator->step();
                for (const auto& event : step.events) if (event.gap) hub->reportGap(*event.gap);
                if (step.error) {
                    simulator->stop(capture->capture_id); session->stop(capture->capture_id); capture.reset();
                    fail(*step.error); hub.reset(); history.reset(); continue;
                }
                for (auto& block : step.blocks) {
                    block.host_received_monotonic_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
                    block.host_clock_id = "std::chrono::steady_clock";
                    auto result = hub->publish(block);
                    if (result.status == PublishStatus::Accepted) {
                        published = true;
                        const auto descriptor = std::find_if(capture->streams.begin(), capture->streams.end(), [&](const auto& d) { return d.key == block.stream; });
                        if (descriptor != capture->streams.end()) diagnostics.observe(block, *descriptor);
                    }
                    else if (result.error) {
                        simulator->stop(capture->capture_id); session->stop(capture->capture_id); capture.reset();
                        fail(*result.error); hub.reset(); history.reset(); break;
                    }
                }
            }
            if (!capture) continue;
            if (now >= nextView) {
                nextView = now + std::chrono::milliseconds(33);
                const auto stats = hub->stats();
                auto traces = buildAcquisitionTraces(*capture, history);
                bool stateChanged = false;
                { std::lock_guard<std::mutex> lock(state->mutex);
                  state->view.traces = std::move(traces); state->view.samples = stats.accepted_samples;
                  state->view.missingSamples = stats.known_missing_samples; state->view.unknownGaps = stats.unknown_gaps;
                  if (published && state->view.acquisition == DeviceAcquisitionState::Starting) {
                      state->view.acquisition = DeviceAcquisitionState::Streaming; stateChanged = true;
                  } }
                if (stateChanged) notify();
            }
        }
    });
    worker_->start();
}

AcquisitionController::~AcquisitionController() {
    recorder_->stop(true);
    tcpSource_->close();
    wifiSource_->close();
    { std::lock_guard<std::mutex> lock(state_->mutex); state_->command = Command::Exit; }
    state_->wake.notify_one();
    worker_->wait(); delete worker_;
}
AcquisitionViewSnapshot AcquisitionController::snapshot() const {
    AcquisitionViewSnapshot view;
    if (activeSource_) {
        view = activeSource_->snapshot();
        if (sourceOpenPending_ || sourceClosePending_) {
            view.connection = DeviceConnectionState::Connecting;
            view.acquisition = DeviceAcquisitionState::Idle;
            view.error = {};
            view.canConfigure = false;
            view.connectionPhase = sourceClosePending_ ? QStringLiteral("stopping") : QString();
        }
        if (sourceStartPending_ && view.acquisition == DeviceAcquisitionState::Idle) view.acquisition = DeviceAcquisitionState::Starting;
        if (sourceStopPending_ && view.acquisition != DeviceAcquisitionState::Faulted)
            view.acquisition = DeviceAcquisitionState::Stopping;
        if (sourceConfigurePending_) { view.canConfigure = false; view.configurationInProgress = true; }
    }
    else { std::lock_guard<std::mutex> lock(state_->mutex); view = state_->view; }
    view.recording = recorder_->snapshot();
    return view;
}
QString AcquisitionController::diagnosticLogPath() const {
    const auto view = snapshot();
    if (!view.diagnosticLogPath.isEmpty()) return view.diagnosticLogPath;
    return view.connection == DeviceConnectionState::Disconnected ? lastDiagnosticLogPath_ : QString();
}
void AcquisitionController::requestOpenLogs() {
    const auto path = diagnosticLogPath();
    emit logLocationRequested(!path.isEmpty() && QFileInfo::exists(path) ? path : AcquisitionDiagnosticLog::directory());
}
bool AcquisitionController::hasActiveTasks() const {
    auto view = snapshot();
    return view.connection == DeviceConnectionState::Connecting || view.configurationInProgress ||
           view.acquisition == DeviceAcquisitionState::Starting || view.acquisition == DeviceAcquisitionState::Streaming ||
           view.acquisition == DeviceAcquisitionState::Stopping || recorder_->isActive();
}
void AcquisitionController::connectSimulator(bool icaMix) {
    cancelSignalWindow();
    if (activeSource_) {
        const auto current = snapshot();
        if (current.connection == DeviceConnectionState::Connected || current.connection == DeviceConnectionState::Connecting || recorder_->isActive()) return;
        activeSource_->close(); activeSource_ = nullptr;
    }
    { std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->view.connection == DeviceConnectionState::Connected || state_->view.connection == DeviceConnectionState::Connecting) return;
      state_->requestedIcaMix = icaMix;
      state_->view.connection = DeviceConnectionState::Connecting; state_->command = Command::Connect; }
    recorder_->clearResult();
    emit stateChanged(); state_->wake.notify_one();
}
bool AcquisitionController::selectSource(AcquisitionSource* source) {
    const auto current = snapshot();
    if (current.connection == DeviceConnectionState::Connected || current.connection == DeviceConnectionState::Connecting || hasActiveTasks()) return false;
    if (activeSource_ && activeSource_ != source) activeSource_->close();
    activeSource_ = source;
    sourceConnectionRevision_ = source->snapshot().connectionRevision;
    sourceOpenPending_ = true;
    sourceClosePending_ = false;
    sourceStartPending_ = sourceStopPending_ = sourceConfigurePending_ = false;
    const auto selection = ++sourceSelectionRevision_;
    recorder_->clearResult();
    // A synchronous state listener may have cancelled before open is queued.
    return sourceSelectionRevision_ == selection && activeSource_ == source && sourceOpenPending_ && !sourceClosePending_;
}
void AcquisitionController::connectTcp(const QString& host, quint16 port, const QString& expectedDeviceId) {
    cancelSignalWindow();
    if (host.trimmed().isEmpty() || port == 0 || !selectSource(tcpSource_)) return;
    tcpSource_->open(host.trimmed(), port, expectedDeviceId.trimmed());
    emit stateChanged();
}
void AcquisitionController::connectWifi(const RtBciWifiOptions& options) {
    cancelSignalWindow();
    if (!selectSource(wifiSource_)) return;
    wifiSource_->open(options);
    emit stateChanged();
}
void AcquisitionController::disconnectDevice() {
    cancelSignalWindow();
    if (activeSource_) {
        const auto view = snapshot();
        if (sourceClosePending_ || view.connection == DeviceConnectionState::Disconnected ||
            recorder_->isActive() || view.configurationInProgress ||
            (view.acquisition != DeviceAcquisitionState::Idle && view.acquisition != DeviceAcquisitionState::Faulted)) return;
        sourceOpenPending_ = false;
        sourceDisconnectionCompletion_ = activeSource_->lastCloseRequest() + 1;
        sourceClosePending_ = true;
        activeSource_->close();
        emit stateChanged();
        return;
    }
    { std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->view.connection != DeviceConnectionState::Connected ||
          (state_->view.acquisition != DeviceAcquisitionState::Idle && state_->view.acquisition != DeviceAcquisitionState::Faulted) || recorder_->isActive()) return;
      state_->view.connection = DeviceConnectionState::Connecting; state_->command = Command::Disconnect; }
    emit stateChanged(); state_->wake.notify_one();
}
void AcquisitionController::configureSimulator(int rate) {
    if (activeSource_) return;
    if (rate != 250 && rate != 500 && rate != 1000) return;
    { std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->view.connection != DeviceConnectionState::Connected || state_->view.acquisition != DeviceAcquisitionState::Idle || recorder_->isActive()) return;
      state_->requestedRate = rate; state_->view.connection = DeviceConnectionState::Connecting; state_->command = Command::Configure; }
    emit stateChanged(); state_->wake.notify_one();
}
void AcquisitionController::configureSampleRate(int rate) {
    if (activeSource_) {
        const auto view = snapshot();
        if (view.connection == DeviceConnectionState::Connected && view.acquisition == DeviceAcquisitionState::Idle &&
            view.canConfigure && !recorder_->isActive()) {
            sourceConfigStartCompletion_ = view.configCompletion;
            sourceConfigurePending_ = true;
            activeSource_->configure(rate);
            emit stateChanged();
        }
        return;
    }
    configureSimulator(rate);
}
void AcquisitionController::startAcquisition() {
    cancelSignalWindow();
    if (activeSource_) {
        const auto view = snapshot();
        if (view.connection == DeviceConnectionState::Connected && view.acquisition == DeviceAcquisitionState::Idle &&
            !view.configurationInProgress && !recorder_->isActive()) {
            recorder_->clearResult();
            sourceStartCompletion_ = view.acquisitionCompletion;
            sourceStartPending_ = true;
            activeSource_->startAcquisition();
            emit stateChanged();
        }
        return;
    }
    { std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->view.connection != DeviceConnectionState::Connected || state_->view.acquisition != DeviceAcquisitionState::Idle || recorder_->isActive()) return;
      state_->view.acquisition = DeviceAcquisitionState::Starting; state_->command = Command::Start; }
    recorder_->clearResult();
    emit stateChanged(); state_->wake.notify_one();
}
void AcquisitionController::stopAcquisition() {
    if (activeSource_) {
        const auto view = snapshot();
        if (view.acquisition == DeviceAcquisitionState::Starting || view.acquisition == DeviceAcquisitionState::Streaming) {
            sourceStopCompletion_ = view.acquisitionCompletion;
            sourceStartPending_ = false; sourceStopPending_ = true;
            activeSource_->stopAcquisition();
            emit stateChanged();
        }
        return;
    }
    { std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->view.acquisition != DeviceAcquisitionState::Starting && state_->view.acquisition != DeviceAcquisitionState::Streaming) return;
      state_->view.acquisition = DeviceAcquisitionState::Stopping; state_->command = Command::Stop; }
    emit stateChanged(); state_->wake.notify_one();
}
void AcquisitionController::startRecording(const QString& outputDirectory) {
    if (activeSource_) {
        // A queued stop is already visible to the GUI even before the source
        // worker leaves Streaming. Respect that projection for modal-dialog
        // returns and direct callers as well as the record button.
        if (recorder_->isActive() || snapshot().acquisition != DeviceAcquisitionState::Streaming) return;
        const auto context = activeSource_->captureContext();
        if (context) recorder_->start(outputDirectory, context->device, context->capture, context->hub);
        return;
    }
    std::shared_ptr<SignalHub> hub; DeviceDescriptor device; std::optional<CaptureDescriptor> capture;
    { std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->view.acquisition != DeviceAcquisitionState::Streaming || recorder_->isActive()) return;
      hub = state_->hub; capture = state_->capture; device = state_->device; }
    if (capture) recorder_->start(outputDirectory, device, *capture, std::move(hub));
}
QJsonArray AcquisitionController::signalStreams() const {
    std::optional<acquisition::CaptureDescriptor> capture;
    if(activeSource_){const auto context=activeSource_->captureContext();if(context)capture=context->capture;}
    else {std::lock_guard<std::mutex> lock(state_->mutex);capture=state_->capture;}
    QJsonArray result;if(!capture)return result;
    for(const auto& d:capture->streams)result.append(QJsonObject{
        {"key",QString::fromStdString(d.key.session_id.value)+"/"+QString::number(d.key.stream_id)},
        {"name",QString::fromStdString(d.name)},{"fs",double(d.rate.numerator)/d.rate.denominator},{"channels",int(d.channels.size())}});
    return result;
}
QString AcquisitionController::signalSourceId() const {
    std::optional<acquisition::CaptureDescriptor> capture;
    if (activeSource_) {
        if (const auto context = activeSource_->captureContext()) capture = context->capture;
    } else {
        std::lock_guard<std::mutex> lock(state_->mutex); capture = state_->capture;
    }
    return capture ? QString::fromStdString(capture->capture_id.value) + "/" + QString::number(capture->generation) : QString();
}
void AcquisitionController::cancelSignalWindow(){++signalReadRevision_;}
void AcquisitionController::requestRawSignalWindow(quint64 requestId, const QString& stream, double seconds) {
    std::optional<acquisition::CaptureDescriptor> capture;
    std::shared_ptr<acquisition::SignalConsumer> history;
    if (activeSource_) {
        if (const auto context = activeSource_->captureContext()) {
            capture = context->capture; history = context->history;
        }
    } else {
        std::lock_guard<std::mutex> lock(state_->mutex);
        capture = state_->capture; history = state_->history;
    }
    if (!capture || !history) {
        emit rawSignalWindowFailed(requestId, makeError("signal.no_samples"));
        return;
    }
    const QPointer<AcquisitionController> self(this);
    QThreadPool::globalInstance()->start([self, requestId, capture = *capture, history, stream, seconds] {
        WaveformSlice raw; AppError error;
        try {
            if (!stream.isEmpty() && std::none_of(capture.streams.begin(), capture.streams.end(), [&](const auto& descriptor) {
                return QString::fromStdString(descriptor.key.session_id.value) + "/" + QString::number(descriptor.key.stream_id) == stream;
            })) throw std::invalid_argument("signal.invalid_window");
            raw = acquisitionSignalWindow(capture, history, stream, seconds);
        }
        catch (const std::exception& e) {
            const QString code = QString::fromUtf8(e.what());
            error = makeError(code.startsWith("signal.") ? code : QString("signal.invalid_window"));
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, requestId, raw = std::move(raw), error, capture] {
            if (!self) return;
            // A reconnect/reconfiguration can finish while this worker is reading.
            std::optional<acquisition::CaptureDescriptor> current;
            if (self->activeSource_) {
                if (const auto context = self->activeSource_->captureContext()) current = context->capture;
            } else {
                std::lock_guard<std::mutex> lock(self->state_->mutex); current = self->state_->capture;
            }
            if (!current || current->capture_id.value != capture.capture_id.value || current->generation != capture.generation)
                emit self->rawSignalWindowFailed(requestId, makeError("signal.no_samples"));
            else if (!error.code.isEmpty()) emit self->rawSignalWindowFailed(requestId, error);
            else emit self->rawSignalWindowReady(requestId, raw);
        }, Qt::QueuedConnection);
    });
}
void AcquisitionController::requestSignalWindow(const QString& stream,const ProcessingViewState& settings,bool causal,double seconds){
    signalProcessingEnabled_=true;lastSignalStream_=stream;lastSignalSettings_=settings;
    lastSignalCausal_=causal;lastSignalSeconds_=seconds;
    if(signalReadBusy_)return;
    std::optional<acquisition::CaptureDescriptor> capture;std::shared_ptr<acquisition::SignalConsumer> history;
    if(activeSource_){const auto context=activeSource_->captureContext();if(context){capture=context->capture;history=context->history;}}
    else {std::lock_guard<std::mutex> lock(state_->mutex);capture=state_->capture;history=state_->history;}
    if(!capture || !history)return;
    signalReadBusy_=true;const auto revision=signalReadRevision_;const QPointer<AcquisitionController> self(this);const auto processor=processingWorker_;
    QThreadPool::globalInstance()->start([self,revision,capture=*capture,history,stream,settings,causal,seconds,processor]{
        WaveformSlice raw,processed;AppError error;
        try { raw=acquisitionSignalWindow(capture,history,stream,seconds);
            std::lock_guard<std::mutex> lock(processor->mutex);processed=processor->processor.process(raw,settings,causal);
        } catch(const std::exception& e){const QString code=QString::fromUtf8(e.what());error=makeError(code.startsWith("signal.")?code:QString("signal.invalid_window"));}
        if(!self)return;
        QMetaObject::invokeMethod(self,[self,revision,raw=std::move(raw),processed=std::move(processed),error]{
            if(!self)return;self->signalReadBusy_=false;
            if(revision!=self->signalReadRevision_)return;
            if(!raw.data.empty())emit self->signalWindowReady(raw,processed);
            if(!error.code.isEmpty())emit self->signalWindowFailed(error);
        },Qt::QueuedConnection);
    });
}
void AcquisitionController::stopRecording() { recorder_->stop(); }
void AcquisitionController::requestShutdown() {
    { std::lock_guard<std::mutex> lock(state_->mutex); state_->shuttingDown = true; state_->shutdownFailed = false; }
    if (snapshot().connection == DeviceConnectionState::Connecting) disconnectDevice();
    stopAcquisition();
    const auto view = snapshot();
    if (view.acquisition == DeviceAcquisitionState::Idle || view.acquisition == DeviceAcquisitionState::Faulted)
        recorder_->stop(view.acquisition == DeviceAcquisitionState::Faulted);
    emit stateChanged();
}
} // namespace bf
