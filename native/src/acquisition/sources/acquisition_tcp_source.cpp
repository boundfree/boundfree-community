#include "acquisition/sources/acquisition_tcp_source.h"
#include "acquisition/control/acquisition_diagnostic_log.h"

#include "acquisition/transport/bfsp_tcp_client.h"
#include "acquisition/control/acquisition_format.h"
#include "acquisition/control/acquisition_view.h"
#include "core/app_error.h"

#include <QJsonArray>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

namespace bf {
namespace {

using namespace acquisition;
using TcpClient = transport::BfspTcpClient;
using Context = AcquisitionTcpSource::CaptureContext;
constexpr int DescriptorWaitMs = 5000;
// Match BfspTcpClient's advertised receive profile (protocol upper bound: 64).
// This is independent of the number of channels rendered in the display.
constexpr int ReceiveProfileMaxStreams = 16;

struct SharedState {
    mutable std::mutex mutex;
    AcquisitionViewSnapshot view;
    std::optional<Context> context;
    std::atomic_bool notificationPending{false};
};

QString requestId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower(); }

bool integer32(const QJsonValue& value, quint32& result, bool positive = false)
{
    if (!value.isDouble()) return false;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < (positive ? 1.0 : 0.0) ||
        number > std::numeric_limits<quint32>::max() || std::floor(number) != number) return false;
    result = quint32(number);
    return true;
}

bool integer64String(const QJsonValue& value, quint64& result)
{
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text.front() == QLatin1Char('0'))) return false;
    quint64 number = 0;
    for (const auto c : text) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
        const auto digit = quint64(c.unicode() - '0');
        if (number > (std::numeric_limits<quint64>::max() - digit) / 10) return false;
        number = number * 10 + digit;
    }
    result = number;
    return true;
}

AppError modelError(const DeviceError& error)
{
    QJsonObject params;
    for (const auto& item : error.params)
        params.insert(QString::fromStdString(item.first), QString::fromStdString(item.second));
    return makeError(QString::fromStdString(error.code), params, error.retryable);
}

class TcpWorker final : public QObject {
public:
    TcpWorker(std::shared_ptr<SharedState> shared, QPointer<AcquisitionTcpSource> owner, VersionInfo version)
        : shared_(std::move(shared)), owner_(owner), version_(std::move(version)), budget_(256 * 1024 * 1024) {}

    void setup()
    {
        client_ = new TcpClient(this);
        client_->setClientIdentity(version_.productName.isEmpty() ? QStringLiteral("BoundFree Community") : version_.productName,
                                   version_.version.isEmpty() ? QStringLiteral("unknown") : version_.version);
        connect(client_, &TcpClient::connected, this, [this](quint64 gen, const QJsonObject& ack) {
            if (gen == generation_) connected(ack);
        });
        connect(client_, &TcpClient::responseReceived, this, [this](quint64 gen, const QJsonObject& response) {
            if (gen == generation_) responseReceived(response);
        });
        connect(client_, &TcpClient::descriptorReceived, this, [this](quint64 gen, quint32 stream, const QJsonObject& object) {
            if (gen == generation_) descriptorReceived(stream, object);
        });
        // Both objects live in this thread: no queued raw DATA or GUI copies.
        connect(client_, &TcpClient::dataReceived, this,
                [this](quint64 gen, quint32 stream, const QJsonObject& meta, const QByteArray& payload) {
            if (gen == generation_) dataReceived(stream, meta, payload);
        }, Qt::DirectConnection);
        connect(client_, &TcpClient::streamEnded, this, [this](quint64 gen, quint32 stream, const QJsonObject& end) {
            if (gen == generation_) streamEnded(stream, end);
        });
        connect(client_, &TcpClient::eventReceived, this, [this](quint64 gen, quint32 stream, const QJsonObject& event) {
            if (gen == generation_) eventReceived(stream, event);
        });
        connect(client_, &TcpClient::statusReceived, this, [this](quint64 gen, const QJsonObject& status) {
            if (gen == generation_) statusReceived(status);
        });
        connect(client_, &TcpClient::errorOccurred, this, [this](quint64 gen, const QString& code, const QJsonObject& params) {
            if (gen == generation_) errorReceived(code, params);
        });
        connect(client_, &TcpClient::stateChanged, this, [this](quint64 gen, TcpClient::State state) {
            if (gen == generation_ && state == TcpClient::State::Faulted) fault("device.disconnected", {});
        });
        timer_ = new QTimer(this);
        timer_->setInterval(33);
        connect(timer_, &QTimer::timeout, this, [this] { refresh(); });
        timer_->start();
        descriptorTimer_ = new QTimer(this);
        descriptorTimer_->setSingleShot(true);
        connect(descriptorTimer_, &QTimer::timeout, this, [this] {
            if (stage_ != Stage::Preparing || captureId_.isEmpty()) return;
            setError("request.timeout", {{"method", "capture.descriptor"}});
            cancelRequested_ = true;
            discardCapture();
        });
    }

    void open(const QString& host, quint16 port, const QString& expectedDevice)
    {
        if (stage_ != Stage::Disconnected && stage_ != Stage::Faulted) {
            setError("device.busy");
            return;
        }
        clearSession();
        diagnostics_.open("bfsp.tcp", std::nullopt, {{"host", host}, {"port", port}});
        ++generation_;
        ++connectionRevision_;
        stage_ = Stage::Connecting;
        update([&](auto& view) {
            view = {};
            view.connectionRevision = connectionRevision_;
            view.disconnectionCompletion = disconnectionCompletion_;
            view.connection = DeviceConnectionState::Connecting;
            view.acquisition = DeviceAcquisitionState::Idle;
            view.sampleRate = 0;
            view.exactRate = {0, 0};
            view.canConfigure = false;
        });
        client_->open(generation_, host, port, expectedDevice);
    }

    void close(quint64 request = 0)
    {
        disconnectionCompletion_ = std::max(disconnectionCompletion_, request);
        if (stage_ == Stage::Disconnected) {
            update([&](auto& view) { view.disconnectionCompletion = disconnectionCompletion_; });
            return;
        }
        const bool interrupted = activeOperation();
        const bool faulted = stage_ == Stage::Faulted;
        if (interrupted) terminateHub("device.disconnected");
        ++generation_;
        client_->close(generation_);
        clearSession();
        stage_ = Stage::Disconnected;
        diagnostics_.close();
        update([&](auto& view) {
            view.connection = DeviceConnectionState::Disconnected;
            view.disconnectionCompletion = disconnectionCompletion_;
            view.acquisition = interrupted || faulted ? DeviceAcquisitionState::Faulted : DeviceAcquisitionState::Idle;
            view.canConfigure = false;
            if (view.configurationInProgress) { view.configurationInProgress = false; ++view.configCompletion; }
            if (interrupted) ++view.acquisitionCompletion;
            if (interrupted) view.error = makeError("device.disconnected");
        });
    }

    void shutdown() { close(); }

    void configure(int rate)
    {
        if (stage_ != Stage::Idle || !canConfigure_ || !rateAllowed(rate)) {
            setError(stage_ == Stage::Idle ? "capability.unsupported" : "device.busy");
            if (stage_ != Stage::Validating && stage_ != Stage::Applying && stage_ != Stage::Unknown)
                completeConfiguration();
            return;
        }
        candidate_ = config_;
        auto groups = candidate_.value("groups").toArray();
        auto group = groups.at(0).toObject();
        group.insert("rate_num", rate);
        group.insert("rate_den", 1);
        groups[0] = group;
        candidate_.insert("groups", groups);
        stage_ = Stage::Validating;
        update([&](auto& view) { view.error = {}; view.canConfigure = false; view.configurationInProgress = true; });
        send("config.validate", {{"candidate", candidate_}, {"expected_revision", double(configRevision_)}});
    }

    void start()
    {
        if (stage_ != Stage::Idle) {
            update([](auto& view) { view.error = makeError("device.busy"); ++view.acquisitionCompletion; });
            return;
        }
        diagnostics_.setSimulated(deviceSimulated_);
        cancelRequested_ = false;
        prepareResult_ = {};
        descriptors_.clear();
        expectedStreams_.clear();
        ended_.clear();
        nextSamples_.clear();
        futureGaps_.clear();
        captureId_.clear();
        receivedData_ = false;
        diagnostics_.flushStatistics(); diagnostics_.write("stream_start_requested");
        stage_ = Stage::Preparing;
        update([&](auto& view) {
            view.acquisition = DeviceAcquisitionState::Starting;
            view.samples = view.missingSamples = view.unknownGaps = 0;
            view.totalChannels = 0;
            view.traces.clear();
            view.simulated = deviceSimulated_;
            view.error = {};
            view.canConfigure = false;
        });
        send("capture.prepare", {{"expected_config_revision", double(configRevision_)}});
    }

    void stop()
    {
        if (stage_ == Stage::Prepared) {
            cancelRequested_ = true;
            discardCapture();
            return;
        }
        if (stage_ == Stage::Preparing || stage_ == Stage::Accepting || stage_ == Stage::Unknown) {
            cancelRequested_ = true;
            update([](auto& view) { view.acquisition = DeviceAcquisitionState::Stopping; });
            if (stage_ == Stage::Preparing && !captureId_.isEmpty()) discardCapture();
            return;
        }
        if (stage_ == Stage::Starting || stage_ == Stage::Streaming) {
            cancelRequested_ = true;
            stopCapture();
        } else if (stage_ == Stage::Idle) {
            update([](auto& view) { ++view.acquisitionCompletion; });
        }
    }

private:
    enum class Stage { Disconnected, Connecting, Checking, Idle, Validating, Applying, Preparing, Prepared,
                       Accepting, Starting, Streaming, Stopping, Discarding, Unknown, Faulted };

    template <typename F>
    void update(F change)
    {
        { std::lock_guard<std::mutex> lock(shared_->mutex); change(shared_->view); shared_->view.diagnosticLogPath = diagnostics_.path(); }
        notify();
    }

    void notify()
    {
        if (!owner_ || shared_->notificationPending.exchange(true)) return;
        const auto shared = shared_;
        const auto owner = owner_;
        QMetaObject::invokeMethod(owner.data(), [shared, owner] {
            shared->notificationPending.store(false);
            if (owner) emit owner->stateChanged();
        }, Qt::QueuedConnection);
    }

    void setError(const QString& code, const QJsonObject& params = {}, bool retryable = false)
    { diagnostics_.write("error", {{"code", code}, {"params", params}}); update([&](auto& view) { view.error = makeError(code, params, retryable); }); }

    bool activeOperation() const
    {
        return stage_ == Stage::Preparing || stage_ == Stage::Prepared || stage_ == Stage::Accepting || stage_ == Stage::Starting ||
               stage_ == Stage::Streaming || stage_ == Stage::Stopping || stage_ == Stage::Discarding || stage_ == Stage::Unknown;
    }

    void send(const QString& method, const QJsonObject& params = {})
    {
        diagnostics_.write("command", {{"method", method}});
        const auto id = requestId();
        pending_.insert(id, method);
        if (method == "capture.start") lastStartRequest_ = id;
        client_->command(generation_, id, method, params);
    }

    void clearSession()
    {
        if (descriptorTimer_) descriptorTimer_->stop();
        pending_.clear();
        unknownRequest_.clear();
        terminalFailureQuery_ = false;
        lastStartRequest_.clear();
        sessionId_.clear();
        captureId_.clear();
        descriptors_.clear();
        expectedStreams_.clear();
        ended_.clear();
        nextSamples_.clear();
        futureGaps_.clear();
        capture_.reset();
        hub_.reset();
        history_.reset();
        config_ = {};
        schema_ = {};
        candidate_ = {};
        canConfigure_ = false;
        deviceSimulated_ = false;
        cancelRequested_ = false;
        receivedData_ = false;
        { std::lock_guard<std::mutex> lock(shared_->mutex); shared_->context.reset(); }
    }

    void connected(const QJsonObject& ack)
    {
        sessionId_ = ack.value("session_id").toString();
        const auto device = ack.value("device").toObject();
        auto id = device.value("device_id").toString();
        if (id.isEmpty()) id = QStringLiteral("unknown:") + sessionId_;
        device_.device_id = DeviceId{id.toStdString()};
        device_.boot_id = BootId{ack.value("boot_id").toString().toStdString()};
        device_.weak_identity = device.value("identity_strength").toString() != "strong";
        device_.generated_boot_id = false;
        device_.manufacturer = device.value("vendor").toString().toStdString();
        device_.model = device.value("model").toString().toStdString();
        device_.firmware = device.value("firmware").toString().toStdString();
        const auto capabilities = device.value("capabilities").toObject();
        schema_ = capabilities.value("config_schema").toObject();
        config_ = capabilities.value("effective_config").toObject();
        if (config_.isEmpty()) config_ = capabilities.value("current_config").toObject();
        canConfigure_ = capabilities.value("can_configure").toBool() && hasRateCapability();
        const auto name = device.value("name").toString();
        deviceSimulated_ = capabilities.value("simulated").toBool(false);
        diagnostics_.setSimulated(deviceSimulated_);
        update([&](auto& view) { view.deviceName = name; view.simulated = deviceSimulated_; });
        stage_ = Stage::Checking;
        send("device.get_status");
    }

    bool hasRateCapability() const
    {
        const auto configured = config_.value("groups").toArray();
        const auto groups = schema_.value("groups").toArray();
        if (configured.size() != 1 || !configured.at(0).isObject() || groups.isEmpty()) return false;
        const auto groupId = configured.at(0).toObject().value("group_id").toString();
        if (groupId.isEmpty()) return false;
        int matches = 0;
        for (const auto& entry : groups) {
            const auto group = entry.toObject();
            if (group.value("group_id").toString() != groupId) continue;
            ++matches;
            const auto rate = group.value("rate_hz").toObject();
            const auto choices = rate.value("enum").toArray();
            const bool range = rate.value("min").isDouble() && rate.value("max").isDouble() &&
                               std::isfinite(rate.value("min").toDouble()) && std::isfinite(rate.value("max").toDouble()) &&
                               rate.value("min").toDouble() > 0 && rate.value("max").toDouble() >= rate.value("min").toDouble();
            if (!range && choices.isEmpty()) return false;
        }
        return matches == 1;
    }

    bool rateAllowed(int rate) const
    {
        if (rate <= 0 || !hasRateCapability()) return false;
        const auto groupId = config_.value("groups").toArray().at(0).toObject().value("group_id").toString();
        for (const auto& entry : schema_.value("groups").toArray()) {
            const auto group = entry.toObject();
            if (group.value("group_id").toString() != groupId) continue;
            const auto declared = group.value("rate_hz").toObject();
            if (declared.contains("min") && declared.contains("max") &&
                (rate < declared.value("min").toDouble() || rate > declared.value("max").toDouble())) return false;
            const auto values = declared.value("enum").toArray();
            if (!values.isEmpty() && !values.contains(rate)) return false;
            return true;
        }
        return false;
    }

    void refreshRate()
    {
        const auto groups = config_.value("groups").toArray();
        if (groups.size() != 1) return;
        const auto group = groups.at(0).toObject();
        quint32 num = 0, den = 0;
        if (!integer32(group.value("rate_num"), num, true) || !integer32(group.value("rate_den"), den, true)) return;
        update([&](auto& view) {
            view.exactRate = {num, den};
            view.sampleRate = num % den == 0 && num / den <= quint32(std::numeric_limits<int>::max()) ? int(num / den) : 0;
        });
    }

    void responseReceived(const QJsonObject& response)
    {
        const auto id = response.value("request_id").toString();
        const auto method = pending_.value(id);
        if (method.isEmpty()) return;
        if (!response.value("ok").toBool() && (method == "capture.stop" || method == "capture.discard")) {
            const auto error = response.value("error").toObject();
            unknownRequest_ = id;
            terminalFailureQuery_ = true;
            stage_ = Stage::Unknown;
            setError(error.value("code").toString("bfsp.invalid_payload"), error.value("params").toObject(),
                     error.value("retryable").toBool());
            send("device.get_status");
            return;
        }
        pending_.remove(id);
        handleResponse(method, response.value("ok").toBool(), response.value("result").toObject(),
                       response.value("error").toObject());
    }

    void handleResponse(const QString& method, bool ok, const QJsonObject& result, const QJsonObject& error)
    {
        if (!ok) {
            const auto code = error.value("code").toString("bfsp.invalid_payload");
            setError(code, error.value("params").toObject(), error.value("retryable").toBool());
            if (method == "capture.prepare") {
                stage_ = Stage::Idle;
                update([&](auto& view) { view.acquisition = DeviceAcquisitionState::Idle;
                    view.canConfigure = canConfigure_; ++view.acquisitionCompletion; });
            }
            else if (method == "stream.accept" || method == "capture.start") discardCapture();
            else if ((method == "config.apply" || method == "config.validate") &&
                     (code == "device.config_partial" || code == "device.config_conflict")) {
                // Status alone cannot refresh the effective configuration.
                // Keep the device's failure cause and require a new handshake.
                fault(code, error.value("params").toObject());
            }
            else if (method == "capture.stop" || method == "capture.discard")
                fault("device.invalid_state", {{"method", method}});
            else if (method == "device.get_status") fault("request.timeout", {{"method", method}});
            else {
                stage_ = Stage::Idle;
                if (method == "config.validate" || method == "config.apply") completeConfiguration();
                else idle();
            }
            return;
        }
        if (method == "device.get_status") { applyStatus(result); return; }
        if (method == "config.validate") {
            if (!result.value("errors").isArray() || !result.value("warnings").isArray() ||
                !result.value("errors").toArray().isEmpty() || !result.value("effective_config").isObject()) {
                setError("capability.unsupported"); stage_ = Stage::Idle; completeConfiguration(); return;
            }
            candidate_ = result.value("effective_config").toObject();
            stage_ = Stage::Applying;
            send("config.apply", {{"candidate", candidate_}, {"expected_revision", double(configRevision_)}});
        } else if (method == "config.apply") {
            quint32 revision = 0;
            if (!integer32(result.value("config_revision"), revision, true) || revision <= configRevision_ ||
                !result.value("effective_config").isObject()) {
                fault("bfsp.invalid_payload", {{"field", "config.apply"}}); return;
            }
            configRevision_ = revision;
            config_ = result.value("effective_config").toObject();
            canConfigure_ = canConfigure_ && hasRateCapability();
            stage_ = Stage::Idle;
            refreshRate(); completeConfiguration();
        } else if (method == "capture.prepare") {
            prepareResult_ = result;
            captureId_ = result.value("capture_id").toString();
            expectedStreams_.clear();
            for (const auto& item : result.value("stream_ids").toArray()) {
                quint32 stream = 0;
                if (!integer32(item, stream, true) || expectedStreams_.contains(stream)) {
                    fault("bfsp.invalid_payload", {{"field", "stream_ids"}}); return;
                }
                expectedStreams_.insert(stream);
            }
            if (captureId_.isEmpty() || expectedStreams_.isEmpty() || expectedStreams_.size() > ReceiveProfileMaxStreams) {
                fault("bfsp.invalid_payload", {{"field", "capture.prepare"}}); return;
            }
            if (cancelRequested_) { discardCapture(); return; }
            if (descriptors_.size() < size_t(expectedStreams_.size())) descriptorTimer_->start(DescriptorWaitMs);
            maybePrepared();
        } else if (method == "stream.accept") {
            if (!result.value("accepted").toBool()) { fault("bfsp.invalid_payload", {{"field", "stream.accept"}}); return; }
            if (cancelRequested_) discardCapture();
            else { stage_ = Stage::Starting; send("capture.start", {{"capture_id", captureId_}}); }
        } else if (method == "capture.start") {
            // Streaming is announced only after a successfully published block.
            if (cancelRequested_ && stage_ != Stage::Stopping) stopCapture();
        } else if (method == "capture.stop" || method == "capture.discard") {
            finishCapture();
        }
    }

    void applyStatus(const QJsonObject& status)
    {
        quint32 revision = 0;
        if (!integer32(status.value("config_revision"), revision)) {
            fault("bfsp.invalid_payload", {{"field", "status.config_revision"}}); return;
        }
        const auto state = status.value("state").toString();
        if (stage_ == Stage::Checking) {
            if (state != "Idle" || !status.value("active_capture_id").isNull()) {
                fault("device.busy", {{"state", state}}); return;
            }
            configRevision_ = revision;
            stage_ = Stage::Idle;
            diagnostics_.write("connected");
            refreshRate();
            update([&](auto& view) {
                view.connection = DeviceConnectionState::Connected;
                view.acquisition = DeviceAcquisitionState::Idle;
                view.error = {};
                view.canConfigure = canConfigure_;
                view.configRevision = configRevision_;
            });
            return;
        }
        if (stage_ != Stage::Unknown) return;
        if (unknownRequest_.isEmpty() || !pending_.contains(unknownRequest_)) {
            fault("device.invalid_state", {{"field", "unknown_request"}}); return;
        }
        const auto last = status.value("last_operation").toObject();
        if (last.value("request_id").toString() != unknownRequest_) {
            if (terminalFailureQuery_) fault("device.invalid_state", {{"field", "last_operation"}});
            return;
        }
        const auto operationState = last.value("state").toString();
        if (operationState != "completed" && operationState != "failed") {
            if (terminalFailureQuery_) fault("device.invalid_state", {{"field", "last_operation.state"}});
            return;
        }
        const auto method = pending_.take(unknownRequest_);
        unknownRequest_.clear();
        if (terminalFailureQuery_ && (operationState != "failed" || last.value("method").toString() != method)) {
            fault("device.invalid_state", {{"field", "last_operation.method"}}); return;
        }
        terminalFailureQuery_ = false;
        if (method == "config.apply" && operationState == "completed") {
            quint32 appliedRevision = 0;
            if (!integer32(last.value("result").toObject().value("config_revision"), appliedRevision, true) ||
                appliedRevision != revision) {
                fault("bfsp.invalid_payload", {{"field", "status.config_revision"}}); return;
            }
        } else {
            configRevision_ = revision;
        }
        if ((state == "Prepared" || state == "Streaming" || state == "Starting" || state == "Stopping") &&
            !captureId_.isEmpty() && status.value("active_capture_id").toString() != captureId_) {
            fault("bfsp.invalid_payload", {{"field", "active_capture_id"}}); return;
        }
        if (operationState == "failed" && (method == "capture.stop" || method == "capture.discard")) {
            const auto failure = last.value("error").toObject();
            setError(failure.value("code").toString("device.invalid_state"), failure.value("params").toObject(),
                     failure.value("retryable").toBool());
            restoreAfterFailedTerminal(method, state);
            return;
        }
        if (method == "capture.prepare") stage_ = Stage::Preparing;
        else if (method == "stream.accept") stage_ = Stage::Accepting;
        else if (method == "capture.start") stage_ = receivedData_ ? Stage::Streaming : Stage::Starting;
        else if (method == "capture.stop") stage_ = Stage::Stopping;
        else if (method == "capture.discard") stage_ = Stage::Discarding;
        else if (method == "config.apply") stage_ = Stage::Applying;
        handleResponse(method, operationState == "completed", last.value("result").toObject(), last.value("error").toObject());
        if (cancelRequested_ && (stage_ == Stage::Starting || stage_ == Stage::Streaming)) stopCapture();
    }

    void restoreAfterFailedTerminal(const QString& method, const QString& state)
    {
        cancelRequested_ = false;
        if (method == "capture.stop" && (state == "Streaming" || state == "Starting") && ended_.isEmpty()) {
            stage_ = receivedData_ ? Stage::Streaming : Stage::Starting;
            update([&](auto& view) {
                view.acquisition = receivedData_ ? DeviceAcquisitionState::Streaming : DeviceAcquisitionState::Starting;
                ++view.acquisitionCompletion;
            });
            return;
        }
        if (method == "capture.discard" && state == "Prepared") {
            stage_ = Stage::Prepared;
            update([](auto& view) { view.acquisition = DeviceAcquisitionState::Starting; ++view.acquisitionCompletion; });
            return;
        }
        // A failed response followed by an incompatible state cannot safely
        // restore the transport's lifecycle. End the session visibly instead.
        fault("device.invalid_state", {{"method", method}, {"state", state}});
    }

    void statusReceived(const QJsonObject& status)
    {
        if (stage_ == Stage::Unknown) applyStatus(status);
    }

    void descriptorReceived(quint32 stream, const QJsonObject& json)
    {
        if (stage_ != Stage::Preparing && stage_ != Stage::Prepared && stage_ != Stage::Discarding && stage_ != Stage::Unknown) {
            fault("bfsp.invalid_payload", {{"field", "descriptor.state"}}); return;
        }
        const auto parsed = acquisition_io::descriptorFromJson(json, generation_);
        if (!parsed || parsed.value->key.session_id.value != sessionId_.toStdString() ||
            parsed.value->key.stream_id != stream || descriptors_.count(stream) ||
            (!captureId_.isEmpty() && parsed.value->capture_id.value != captureId_.toStdString()) ||
            (!expectedStreams_.isEmpty() && !expectedStreams_.contains(stream))) {
            fault("bfsp.invalid_payload", {{"field", "descriptor.identity"}}); return;
        }
        descriptors_.emplace(stream, *parsed.value);
        if (parsed.value->clock.simulated) diagnostics_.setSimulated(true);
        if (parsed.value->clock.simulated) update([](auto& view) { view.simulated = true; });
        if (stage_ == Stage::Preparing) maybePrepared();
    }

    void maybePrepared()
    {
        if (prepareResult_.isEmpty() || descriptors_.size() != size_t(expectedStreams_.size())) return;
        descriptorTimer_->stop();
        CaptureDescriptor capture;
        capture.session_id = SessionId{sessionId_.toStdString()};
        capture.capture_id = CaptureId{captureId_.toStdString()};
        capture.generation = generation_;
        for (const auto& entry : descriptors_) {
            if (!expectedStreams_.contains(entry.first) || entry.second.capture_id != capture.capture_id) {
                fault("bfsp.invalid_payload", {{"field", "descriptor.capture"}}); return;
            }
            capture.streams.push_back(entry.second);
        }
        if (cancelRequested_) { discardCapture(); return; }
        try {
            hub_ = std::make_shared<SignalHub>(capture, budget_);
            auto history = hub_->addConsumer(ConsumerKind::History, {64 * 1024 * 1024, 512});
            if (!history) { setError(QString::fromStdString(history.error->code)); discardCapture(); return; }
            history_ = *history.value;
            capture_ = capture;
            for (const auto& stream : capture.streams) nextSamples_[stream.key.stream_id] = 0;
            { std::lock_guard<std::mutex> lock(shared_->mutex); shared_->context = Context{device_, capture, hub_, history_}; }
            update([&](auto& view) {
                view.totalChannels = 0;
                view.simulated = deviceSimulated_;
                for (const auto& stream : capture.streams) {
                    view.totalChannels += stream.channels.size();
                    view.simulated = view.simulated || stream.clock.simulated;
                }
                const auto& first = capture.streams.front().rate;
                view.exactRate = first;
                view.sampleRate = first.numerator % first.denominator == 0 &&
                    first.numerator / first.denominator <= quint32(std::numeric_limits<int>::max())
                    ? int(first.numerator / first.denominator) : 0;
            });
            QJsonArray streams;
            for (const auto& stream : capture.streams) streams.append(QJsonObject{
                {"stream_id", double(stream.key.stream_id)}, {"descriptor_revision", double(stream.revision)}});
            stage_ = Stage::Accepting;
            send("stream.accept", {{"capture_id", captureId_}, {"streams", streams}});
        } catch (const std::exception&) {
            setError("acquisition.invalid_capture"); discardCapture();
        }
    }

    void discardCapture()
    {
        if (captureId_.isEmpty()) { fault("device.invalid_state", {{"method", "capture.discard"}}); return; }
        descriptorTimer_->stop();
        stage_ = Stage::Discarding;
        update([](auto& view) { view.acquisition = DeviceAcquisitionState::Stopping; });
        send("capture.discard", {{"capture_id", captureId_}});
    }

    void stopCapture()
    {
        if (stage_ == Stage::Stopping || captureId_.isEmpty()) return;
        stage_ = Stage::Stopping;
        update([](auto& view) { view.acquisition = DeviceAcquisitionState::Stopping; });
        send("capture.stop", {{"capture_id", captureId_}});
    }

    void dataReceived(quint32 stream, const QJsonObject& meta, const QByteArray& payload)
    {
        quint64 first = 0;
        const auto capture = meta.value("capture_id").toString();
        if (!integer64String(meta.value("first_sample_index"), first)) { fault("bfsp.invalid_payload", {}); return; }
        const auto ack = [this, capture, stream, first] { client_->acknowledgeData(generation_, capture, stream, first); };
        if (!hub_ || !capture_ || capture != captureId_ || !descriptors_.count(stream) || ended_.contains(stream) ||
            (stage_ != Stage::Starting && stage_ != Stage::Streaming && stage_ != Stage::Stopping && stage_ != Stage::Unknown)) {
            ack(); fault("bfsp.invalid_payload", {{"field", "data.identity"}}); return;
        }
        const auto hostNs = static_cast<quint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        auto block = acquisition_io::blockFromPayload(descriptors_.at(stream), payload, budget_, hostNs,
                                                       QStringLiteral("std::chrono::steady_clock"));
        if (!block || block.value->first_sample_index != first || block.value->capture_id.value != capture.toStdString()) {
            ack();
            if (!block && block.error) fault(QString::fromStdString(block.error->code), {});
            else fault("bfsp.invalid_payload", {{"field", "data.identity"}});
            return;
        }
        if (!applyPendingGaps(stream, first)) { ack(); return; }
        const auto& future = futureGaps_[stream];
        if (!future.empty() && *future.front().first_sample_index < first + block.value->sample_count) {
            ack(); fault("bfsp.invalid_payload", {{"field", "event.gap_overlap"}}); return;
        }
        const auto published = hub_->publish(*block.value);
        ack();
        if (published.status != PublishStatus::Accepted) {
            fault(published.error ? QString::fromStdString(published.error->code) : QStringLiteral("acquisition.invalid_block"), {});
            return;
        }
        diagnostics_.observe(*block.value, descriptors_.at(stream));
        nextSamples_[stream] = block.value->first_sample_index + block.value->sample_count;
        if (!applyPendingGaps(stream, std::nullopt)) return;
        receivedData_ = true;
        if (stage_ == Stage::Starting) {
            stage_ = Stage::Streaming;
            diagnostics_.write("stream_started");
            update([](auto& view) { view.acquisition = DeviceAcquisitionState::Streaming; view.error = {}; });
        }
    }

    void streamEnded(quint32 stream, const QJsonObject& end)
    {
        if (!hub_ || !capture_ || !descriptors_.count(stream) || ended_.contains(stream) ||
            end.value("capture_id").toString() != captureId_) {
            fault("bfsp.invalid_payload", {{"field", "stream_end"}}); return;
        }
        const auto& descriptor = descriptors_.at(stream);
        const auto value = end.value("last_sample_exclusive");
        quint64 last = 0;
        if (value.isNull()) hub_->reportUnknownGap(descriptor.key, "stream.end_unknown");
        else if (!integer64String(value, last) || last < nextSamples_[stream]) {
            fault("bfsp.invalid_payload", {{"field", "stream_end.last_sample_exclusive"}}); return;
        } else if (!applyPendingGaps(stream, last)) return;
        else if (!futureGaps_[stream].empty()) {
            fault("bfsp.invalid_payload", {{"field", "stream_end.pending_gap"}}); return;
        } else if (last > nextSamples_[stream]) {
            const SignalGap gap{descriptor.key, nextSamples_[stream], last - nextSamples_[stream], "stream.end_gap"};
            if (const auto error = hub_->reportGap(gap)) { fault(QString::fromStdString(error->code), {}); return; }
            nextSamples_[stream] = last;
        }
        if (!value.isNull() && !end.value("complete").toBool()) hub_->reportUnknownGap(descriptor.key, "stream.incomplete");
        ended_.insert(stream);
        refresh();
        if (stage_ != Stage::Stopping && stage_ != Stage::Unknown) {
            fault("device.disconnected", {{"reason", "unexpected_stream_end"}});
        }
    }

    void eventReceived(quint32 stream, const QJsonObject& event)
    {
        if (event.value("kind").toString() != "data.gap") return;
        if (!hub_ || !descriptors_.count(stream) || ended_.contains(stream)) {
            fault("bfsp.invalid_payload", {{"field", "event.stream"}}); return;
        }
        const auto params = event.value("params").toObject();
        const auto reasonBytes = params.value("reason").toString().toUtf8();
        const std::string reason = reasonBytes.isEmpty() || reasonBytes.size() > 256
            ? "device.reported_gap" : reasonBytes.toStdString();
        quint64 start = 0, count = 0;
        const auto& descriptor = descriptors_.at(stream);
        quint32 revision = 0;
        if (!integer32(event.value("descriptor_revision"), revision, true) || revision != descriptor.revision) {
            fault("bfsp.invalid_payload", {{"field", "event.descriptor_revision"}}); return;
        }
        const auto startValue = event.value("sample_index");
        const auto countValue = event.value("count");
        if (startValue.isNull() || countValue.isNull()) {
            hub_->reportUnknownGap(descriptor.key, reason);
            return;
        }
        if (!integer64String(startValue, start) || !integer64String(countValue, count) || !count ||
            start > std::numeric_limits<quint64>::max() - count) {
            fault("bfsp.invalid_payload", {{"field", "event.gap"}}); return;
        }
        if (start < nextSamples_[stream]) {
            // The affected samples may already have arrived or been inferred
            // missing. Retain the uncertainty rather than rewriting history.
            hub_->reportUnknownGap(descriptor.key, "device.late_gap");
            return;
        }
        auto& gaps = futureGaps_[stream];
        if (gaps.size() >= 64) { fault("bfsp.limit_exceeded", {{"field", "event.gap_queue"}}); return; }
        const auto after = std::lower_bound(gaps.begin(), gaps.end(), start, [](const SignalGap& gap, quint64 first) {
            return *gap.first_sample_index < first;
        });
        if ((after != gaps.end() && start + count > *after->first_sample_index) ||
            (after != gaps.begin() && *(after - 1)->first_sample_index + *(after - 1)->count > start)) {
            fault("bfsp.invalid_payload", {{"field", "event.gap_overlap"}}); return;
        }
        gaps.insert(after, SignalGap{descriptor.key, start, count, reason});
        applyPendingGaps(stream, std::nullopt);
    }

    bool applyPendingGaps(quint32 stream, std::optional<quint64> nextBlockOrEnd)
    {
        auto& gaps = futureGaps_[stream];
        auto& next = nextSamples_[stream];
        const auto& descriptor = descriptors_.at(stream);
        while (!gaps.empty()) {
            const auto gap = gaps.front();
            const quint64 first = *gap.first_sample_index;
            const quint64 end = first + *gap.count;
            if (!nextBlockOrEnd && first != next) break;
            if (nextBlockOrEnd && first >= *nextBlockOrEnd) break;
            if (first < next || (nextBlockOrEnd && end > *nextBlockOrEnd)) {
                fault("bfsp.invalid_payload", {{"field", "event.gap_overlap"}}); return false;
            }
            if (first > next) {
                if (const auto error = hub_->reportGap(SignalGap{descriptor.key, next, first - next, "sample_index.gap"})) {
                    fault(QString::fromStdString(error->code), {}); return false;
                }
                next = first;
            }
            if (const auto error = hub_->reportGap(gap)) { fault(QString::fromStdString(error->code), {}); return false; }
            next = end;
            gaps.erase(gaps.begin());
        }
        return true;
    }

    void refresh()
    {
        if (!hub_ || !capture_ || !history_ || capture_->streams.empty()) return;
        const auto stats = hub_->stats();
        auto traces = buildAcquisitionTraces(*capture_, history_);
        update([&](auto& view) {
            view.samples = stats.accepted_samples;
            view.missingSamples = stats.known_missing_samples;
            view.unknownGaps = stats.unknown_gaps;
            view.traces = std::move(traces);
        });
    }

    void terminateHub(const char* reason)
    {
        diagnostics_.flushStatistics(); diagnostics_.write("stream_interrupted", {{"reason", reason}});
        if (!hub_ || !capture_) return;
        for (const auto& stream : capture_->streams) {
            if (!ended_.contains(stream.key.stream_id)) hub_->reportUnknownGap(stream.key, reason);
        }
        refresh();
        hub_->close();
        { std::lock_guard<std::mutex> lock(shared_->mutex); shared_->context.reset(); }
        hub_.reset(); history_.reset(); capture_.reset();
    }

    void finishCapture()
    {
        diagnostics_.flushStatistics(); diagnostics_.write("stream_stopped");
        descriptorTimer_->stop();
        refresh();
        if (hub_) hub_->close();
        { std::lock_guard<std::mutex> lock(shared_->mutex); shared_->context.reset(); }
        hub_.reset(); history_.reset(); capture_.reset();
        descriptors_.clear(); expectedStreams_.clear(); ended_.clear(); nextSamples_.clear(); futureGaps_.clear();
        captureId_.clear(); prepareResult_ = {}; cancelRequested_ = false;
        lastStartRequest_.clear(); receivedData_ = false;
        stage_ = Stage::Idle;
        update([&](auto& view) { view.acquisition = DeviceAcquisitionState::Idle;
            view.canConfigure = canConfigure_; ++view.acquisitionCompletion; });
    }

    void idle()
    {
        update([&](auto& view) { view.acquisition = DeviceAcquisitionState::Idle; view.canConfigure = canConfigure_; });
    }

    void completeConfiguration()
    {
        update([&](auto& view) {
            view.configurationInProgress = false;
            view.canConfigure = stage_ == Stage::Idle && canConfigure_;
            view.configRevision = configRevision_;
            ++view.configCompletion;
        });
    }

    void errorReceived(const QString& code, const QJsonObject& params)
    {
        if (code == "acquisition.data_gap" || code == "protocol.sequence_gap") return;
        if (code == "acquisition.stream_end_missing") {
            if (hub_ && capture_) {
                for (const auto& stream : capture_->streams) {
                    if (!ended_.contains(stream.key.stream_id)) hub_->reportUnknownGap(stream.key, "stream.end_missing");
                }
            }
            setError(code, params);
            return;
        }
        if (code == "request.timeout" && params.contains("request_id")) {
            const auto id = params.value("request_id").toString();
            const auto method = pending_.value(id);
            if (method.isEmpty()) return;
            if (method == "config.validate") {
                pending_.remove(id);
                stage_ = Stage::Idle;
                setError(code, params);
                completeConfiguration();
                return;
            }
            if (method == "device.get_status") { fault(code, params); return; }
            unknownRequest_ = id;
            terminalFailureQuery_ = false;
            stage_ = Stage::Unknown;
            setError(code, params);
            return;
        }
        if (code == "acquisition.no_data") {
            // A terminal operation owns its recovery until status resolves it.
            // A first-DATA watchdog must not replace that request with start.
            if (stage_ != Stage::Stopping && stage_ != Stage::Discarding &&
                !(stage_ == Stage::Unknown && !unknownRequest_.isEmpty()) && !lastStartRequest_.isEmpty()) {
                unknownRequest_ = lastStartRequest_;
                pending_.insert(unknownRequest_, "capture.start");
                stage_ = Stage::Unknown;
            }
            setError(code, params);
            return;
        }
        if (code == "bfsp.invalid_frame" || code == "bfsp.invalid_payload" || code == "bfsp.timeout") {
            setError(code, params); return;
        }
        setError(code, params);
    }

    void fault(const QString& code, const QJsonObject& params)
    {
        diagnostics_.write("error", {{"code", code}, {"params", params}});
        if (stage_ == Stage::Faulted || stage_ == Stage::Disconnected) return;
        const bool acquisitionActive = activeOperation();
        const auto previousError = [&] { std::lock_guard<std::mutex> lock(shared_->mutex); return shared_->view.error; }();
        descriptorTimer_->stop();
        terminateHub("device.disconnected");
        stage_ = Stage::Faulted;
        ++generation_;
        const auto gen = generation_;
        // Signals may be emitted from inside the client's parser. Close after
        // the callback returns to avoid destroying that parser reentrantly.
        QTimer::singleShot(0, this, [this, gen] { if (gen == generation_) client_->close(gen); });
        pending_.clear();
        update([&](auto& view) {
            view.connection = DeviceConnectionState::Faulted;
            view.acquisition = DeviceAcquisitionState::Faulted;
            view.canConfigure = false;
            if (view.configurationInProgress) { view.configurationInProgress = false; ++view.configCompletion; }
            if (acquisitionActive) ++view.acquisitionCompletion;
            if (!previousError.code.isEmpty() && code == "device.disconnected") view.error = previousError;
            else view.error = makeError(code, params);
        });
    }

    AcquisitionDiagnosticLog diagnostics_;
    std::shared_ptr<SharedState> shared_;
    QPointer<AcquisitionTcpSource> owner_;
    VersionInfo version_;
    MemoryBudget budget_;
    TcpClient* client_ = nullptr;
    QTimer* timer_ = nullptr;
    QTimer* descriptorTimer_ = nullptr;
    Stage stage_ = Stage::Disconnected;
    quint64 generation_ = 0;
    quint64 connectionRevision_ = 0, disconnectionCompletion_ = 0;
    quint32 configRevision_ = 0;
    QString sessionId_, captureId_, unknownRequest_, lastStartRequest_;
    QHash<QString, QString> pending_;
    QJsonObject config_, schema_, candidate_, prepareResult_;
    DeviceDescriptor device_;
    bool canConfigure_ = false, deviceSimulated_ = false, cancelRequested_ = false, receivedData_ = false;
    bool terminalFailureQuery_ = false;
    QSet<quint32> expectedStreams_, ended_;
    std::map<quint32, StreamDescriptor> descriptors_;
    std::map<quint32, quint64> nextSamples_;
    std::map<quint32, std::vector<SignalGap>> futureGaps_;
    std::optional<CaptureDescriptor> capture_;
    std::shared_ptr<SignalHub> hub_;
    std::shared_ptr<SignalConsumer> history_;
};

} // namespace

class AcquisitionTcpSource::Impl {
public:
    Impl(AcquisitionTcpSource* owner, VersionInfo version) : shared(std::make_shared<SharedState>())
    {
        worker = new TcpWorker(shared, owner, std::move(version));
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        thread.start();
        QMetaObject::invokeMethod(worker, [worker = worker] { worker->setup(); }, Qt::QueuedConnection);
    }
    ~Impl()
    {
        QMetaObject::invokeMethod(worker, [worker = worker] { worker->shutdown(); }, Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    std::shared_ptr<SharedState> shared;
    QThread thread;
    TcpWorker* worker = nullptr;
};

AcquisitionTcpSource::AcquisitionTcpSource(VersionInfo version, QObject* parent)
    : AcquisitionSource(parent), d(std::make_unique<Impl>(this, std::move(version))) {}
AcquisitionTcpSource::~AcquisitionTcpSource() = default;

void AcquisitionTcpSource::open(const QString& host, quint16 port, const QString& expectedDeviceId)
{ QMetaObject::invokeMethod(d->worker, [worker = d->worker, host, port, expectedDeviceId] { worker->open(host, port, expectedDeviceId); }, Qt::QueuedConnection); }
void AcquisitionTcpSource::configure(int sampleRate)
{ QMetaObject::invokeMethod(d->worker, [worker = d->worker, sampleRate] { worker->configure(sampleRate); }, Qt::QueuedConnection); }
void AcquisitionTcpSource::startAcquisition()
{ QMetaObject::invokeMethod(d->worker, [worker = d->worker] { worker->start(); }, Qt::QueuedConnection); }
void AcquisitionTcpSource::stopAcquisition()
{ QMetaObject::invokeMethod(d->worker, [worker = d->worker] { worker->stop(); }, Qt::QueuedConnection); }
void AcquisitionTcpSource::close()
{
    const auto request = beginClose();
    QMetaObject::invokeMethod(d->worker, [worker = d->worker, request] { worker->close(request); }, Qt::QueuedConnection);
}
AcquisitionViewSnapshot AcquisitionTcpSource::snapshot() const
{ std::lock_guard<std::mutex> lock(d->shared->mutex); return d->shared->view; }
std::optional<AcquisitionTcpSource::CaptureContext> AcquisitionTcpSource::captureContext() const
{ std::lock_guard<std::mutex> lock(d->shared->mutex); return d->shared->context; }

} // namespace bf
