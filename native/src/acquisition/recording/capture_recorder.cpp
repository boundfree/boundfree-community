#include "acquisition/recording/capture_recorder.h"
#include "acquisition/control/acquisition_format.h"

#include "ipc/sidecar_client.h"

#include <QJsonArray>
#include <QMetaObject>
#include <QPointer>
#include <QStringDecoder>
#include <QSysInfo>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

namespace bf {
namespace {
using namespace acquisition;

QString number(std::uint64_t value) { return QString::number(value); }
QString str(const std::string& value) { return QString::fromStdString(value); }
QString metadataStr(const std::string& value) {
    // A leading U+FEFF is data in a JSON string, not an encoding header.
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    return decoder.decode(QByteArrayView(value.data(), static_cast<qsizetype>(value.size())));
}
QJsonObject blockJson(const QString& handle, const DeviceDescriptor& device,
                      const StreamDescriptor& stream, const SampleBlock& block) {
    const int timeFlags = block.first_timestamp_ticks ? (block.timestamp_estimated ? 3 : 1) : 0;
    return {{"handle", handle}, {"stream", acquisition_io::identityToJson(device, stream)}, {"dtype", acquisition_io::dtypeName(stream.dtype)},
            {"shape", QJsonArray{static_cast<int>(block.channel_count), static_cast<int>(block.sample_count)}},
            {"sample_count", static_cast<double>(block.sample_count)},
            {"first_sample_index", number(block.first_sample_index)},
            {"first_timestamp_ticks", block.first_timestamp_ticks ? QJsonValue(number(*block.first_timestamp_ticks)) : QJsonValue(QJsonValue::Null)},
            {"host_received_monotonic_ns", number(block.host_received_monotonic_ns.value_or(0))},
            {"clock_id", static_cast<double>(block.clock_id)}, {"time_flags", timeFlags},
            {"quality_flags", static_cast<double>(block.quality_flags)}};
}
struct StreamCounters {
    quint64 accepted = 0;
    quint64 written = 0;
    quint64 flushed = 0;
};
bool parseCount(const QJsonObject& result, const char* key, quint64& output) {
    const auto value = result.value(QLatin1String(key));
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text.front() == QLatin1Char('0'))) return false;
    for (const auto c : text) if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
    bool ok = false;
    const auto number = text.toULongLong(&ok);
    if (!ok) return false;
    output = number;
    return true;
}
bool parseCounters(const QJsonObject& result, StreamCounters& counters) {
    return parseCount(result, "accepted_samples", counters.accepted) &&
           parseCount(result, "written_samples", counters.written) &&
           parseCount(result, "flushed_samples", counters.flushed);
}
quint64 saturatedAdd(quint64 lhs, quint64 rhs) {
    return rhs > std::numeric_limits<quint64>::max() - lhs ? std::numeric_limits<quint64>::max() : lhs + rhs;
}
StreamCounters aggregateCounters(const std::map<StreamKey, StreamCounters>& streams) {
    StreamCounters result;
    for (const auto& stream : streams) {
        result.accepted = saturatedAdd(result.accepted, stream.second.accepted);
        result.written = saturatedAdd(result.written, stream.second.written);
        result.flushed = saturatedAdd(result.flushed, stream.second.flushed);
    }
    return result;
}
QString hostClockId() {
    static const QString id = [] {
        const auto boot = QSysInfo::bootUniqueId();
        return boot.isEmpty() ? QStringLiteral("process-") + str(newIdentity()) : QString::fromLatin1(boot.toHex());
    }();
    return id;
}
} // namespace

struct CaptureRecorder::State {
    mutable std::mutex mutex;
    CaptureRecordSnapshot snapshot;
    std::shared_ptr<SignalConsumer> consumer;
    std::atomic_bool stop{false};
    std::atomic_bool interrupted{false};
    bool active = false;
    std::optional<CaptureRecordState> terminal;
};

CaptureRecorder::CaptureRecorder(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent), repoRoot_(std::move(repoRoot)), version_(std::move(version)), state_(std::make_shared<State>()) {}

CaptureRecorder::~CaptureRecorder() {
    stop();
    if (worker_) { worker_->wait(); delete worker_; }
}

CaptureRecordSnapshot CaptureRecorder::snapshot() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->snapshot;
}
bool CaptureRecorder::isActive() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->active;
}
void CaptureRecorder::clearResult() {
    { std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->active) return;
      state_->snapshot = {}; }
    emit stateChanged();
}
void CaptureRecorder::stop(bool interrupted) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (interrupted) state_->interrupted.store(true);
    // Close input before publishing the stop request. A worker observing it
    // must not race a publisher still able to enqueue accepted blocks.
    if (state_->consumer) state_->consumer->stop();
    state_->stop.store(true);
}

bool CaptureRecorder::start(const QString& outputDirectory, const DeviceDescriptor& device,
                            const CaptureDescriptor& capture, std::shared_ptr<SignalHub> hub) {
    if (outputDirectory.isEmpty() || !hub || capture.streams.empty() || isActive()) return false;
    if (worker_) {
        if (worker_->isRunning()) return false;
        delete worker_; worker_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->snapshot = {};
        state_->snapshot.state = CaptureRecordState::Opening;
        state_->consumer.reset();
        state_->active = true;
        state_->terminal.reset();
        state_->stop.store(false);
        state_->interrupted.store(false);
    }
    emit stateChanged();
    auto state = state_;
    const auto root = repoRoot_;
    const auto version = version_;
    const QPointer<CaptureRecorder> owner(this);
    worker_ = QThread::create([state, root, version, device, capture, hub, outputDirectory, owner]() {
        auto notify = [owner] {
            if (!owner) return;
            QMetaObject::invokeMethod(owner.data(), [owner] {
                if (!owner) return;
                emit owner->stateChanged();
            }, Qt::QueuedConnection);
        };
        auto fail = [&](const AppError& error) {
            std::shared_ptr<SignalConsumer> consumer;
            { std::lock_guard<std::mutex> lock(state->mutex);
              consumer = state->consumer;
              state->terminal = CaptureRecordState::Failed; state->snapshot.error = error;
              state->consumer.reset(); }
            if (consumer) hub->removeConsumer(consumer);
        };
        auto cancel = [&] {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->terminal = CaptureRecordState::Off;
        };
        SidecarClient client(root);
        const auto hello = client.startAndHello(version, 10000);
        if (!hello.ok) { fail(hello.error); return; }
        QJsonArray requestedCapabilities{"capture.v1"};
        if (!capture.metadata.empty()) requestedCapabilities.append("capture.metadata.v1");
        const auto capabilities = client.request("hello", {{"requested_capabilities", requestedCapabilities}}, 10000);
        if (!capabilities.ok) { fail(capabilities.error); return; }
        const auto advertised = capabilities.result.value("capabilities").toArray();
        if (!capture.metadata.empty() && !advertised.contains("capture.metadata.v1")) {
            fail(makeError("capture.unavailable", {{"dependency", "capture.metadata.v1"}})); return;
        }
        if (!advertised.contains("capture.v1")) {
            fail(makeError("capture.unavailable", {{"dependency", "capture.v1"}})); return;
        }
        if (state->stop.load()) { cancel(); return; }
        auto subscribed = hub->addConsumer(ConsumerKind::Recording, {64 * 1024 * 1024, 8192});
        if (!subscribed) { fail(makeError("acquisition.consumer_limit")); return; }
        auto consumer = *subscribed.value;
        const auto starts = consumer->stats().start_sample_index;
        { std::lock_guard<std::mutex> lock(state->mutex);
          state->consumer = consumer;
          // Subscription fixes the recording boundary, even if stop arrived
          // before this consumer was visible to the GUI thread.
          if (state->stop.load()) consumer->stop(); }
        QJsonArray streams;
        for (const auto& stream : capture.streams) {
            const auto start = starts.find(stream.key);
            if (start == starts.end()) { fail(makeError("acquisition.stale_block")); return; }
            auto descriptor = acquisition_io::descriptorToJson(device, stream, start->second);
            streams.append(descriptor);
        }
        QJsonObject host{{"source", "std::chrono::steady_clock"}, {"boot_id", hostClockId()}, {"monotonic_base_ns", "0"}};
        QJsonObject openParams{{"out_dir", outputDirectory}, {"host_clock", host}, {"streams", streams}};
        if (!capture.metadata.empty()) {
            QJsonObject metadata;
            for (const auto& entry : capture.metadata) {
                const auto key = metadataStr(entry.first);
                const auto value = metadataStr(entry.second);
                if (key.toUtf8().toStdString() != entry.first || value.toUtf8().toStdString() != entry.second) {
                    fail(makeError("protocol.invalid_params", {{"field", "acquisition_metadata"}, {"reason", "must be UTF-8"}}));
                    return;
                }
                metadata.insert(key, value);
            }
            openParams.insert("acquisition_metadata", metadata);
        }
        auto opened = client.request("capture_open", openParams, 30000);
        if (!opened.ok) { fail(opened.error); return; }
        const QString handle = opened.result.value("handle").toString();
        { std::lock_guard<std::mutex> lock(state->mutex); state->snapshot.path = opened.result.value("path").toString(); }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->stop.load()) state->snapshot.state = CaptureRecordState::Recording;
            else { consumer->stop(); state->snapshot.state = CaptureRecordState::Finalizing; }
        }
        notify();
        std::map<StreamKey, quint64> last(starts.begin(), starts.end());
        std::map<StreamKey, StreamCounters> counters;
        for (const auto& stream : capture.streams) counters.emplace(stream.key, StreamCounters{});
        auto writeUnknownGap = [&](const SignalGap& gap, const StreamDescriptor& descriptor) {
            QJsonObject record{{"event_id", str(newIdentity())}, {"kind", "data.gap"},
                {"sample_index", QJsonValue(QJsonValue::Null)}, {"count", QJsonValue(QJsonValue::Null)},
                {"params", QJsonObject{{"reason", str(gap.reason)}, {"detection", "adapter"}}}};
            auto result = client.request("capture_event", {{"handle", handle}, {"record_type", "event"},
                {"stream", acquisition_io::identityToJson(device, descriptor)}, {"record", record}}, 30000);
            if (!result.ok) { consumer->stop(); fail(result.error); return false; }
            return true;
        };
        bool overflow = false;
        while (true) {
            auto delivery = consumer->pop();
            if (!delivery) {
                const auto stats = consumer->stats();
                if (stats.state == ConsumerState::Failed) { overflow = true; state->stop.store(true); }
                // stop() can close input between pop() and stats(). Drain any
                // blocks accepted in that interval, including during open.
                if (stats.state != ConsumerState::Active && stats.queued_blocks == 0) break;
                QThread::msleep(20);
                continue;
            }
            const auto& block = *delivery->block;
            const StreamDescriptor* descriptor = nullptr;
            for (const auto& stream : capture.streams) if (stream.key == block.stream) { descriptor = &stream; break; }
            if (!descriptor) { fail(makeError("acquisition.stale_block")); return; }
            for (const auto& gap : delivery->gaps_before) {
                // The sidecar records known gaps from absolute sample indices.
                if (!gap.count && !writeUnknownGap(gap, *descriptor)) return;
            }
            if (!block.host_received_monotonic_ns) { consumer->stop(); fail(makeError("acquisition.invalid_block", {{"field", "host_received_monotonic_ns"}})); return; }
            auto encoded = acquisition_io::encodeRawSamples(block);
            if (!encoded) {
                QJsonObject params;
                for (const auto& item : encoded.error->params)
                    params.insert(str(item.first), str(item.second));
                consumer->stop(); fail(makeError(str(encoded.error->code), params)); return;
            }
            auto response = client.requestBinary("capture_append", blockJson(handle, device, *descriptor, block), *encoded.value, nullptr, 30000);
            if (!response.ok) { consumer->stop(); fail(response.error); return; }
            StreamCounters streamCounters;
            quint64 lastAccepted = 0;
            if (!parseCounters(response.result, streamCounters) ||
                !parseCount(response.result, "last_accepted_sample_exclusive", lastAccepted)) {
                consumer->stop(); fail(makeError("ipc.invalid_frame", {{"reason", "invalid capture counters"}})); return;
            }
            counters[block.stream] = streamCounters;
            last[block.stream] = lastAccepted;
            const auto total = aggregateCounters(counters);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->snapshot.acceptedSamples = total.accepted;
                state->snapshot.writtenSamples = total.written;
                state->snapshot.flushedSamples = total.flushed;
            }
        }
        consumer->stop();
        { std::lock_guard<std::mutex> lock(state->mutex); state->snapshot.state = CaptureRecordState::Finalizing; }
        notify();
        const auto stats = consumer->stats();
        const auto pendingGaps = consumer->pendingGaps();
        hub->removeConsumer(consumer);
        overflow = overflow || stats.state == ConsumerState::Failed;
        const bool incomplete = overflow || state->interrupted.load();
        QJsonArray summaries;
        for (const auto& stream : capture.streams) {
            const auto boundary = last[stream.key];
            quint64 terminal = boundary;
            bool unknownGap = incomplete;
            for (const auto& gap : pendingGaps) {
                if (gap.stream != stream.key) continue;
                if (!gap.count && !writeUnknownGap(gap, stream)) return;
                if (gap.count && gap.first_sample_index && *gap.first_sample_index == terminal)
                    terminal += *gap.count;
                else unknownGap = true;
            }
            summaries.append(QJsonObject{{"stream", acquisition_io::identityToJson(device, stream)},
                {"last_accepted_sample_exclusive", number(boundary)},
                {"terminal_sample_exclusive", unknownGap ? QJsonValue(QJsonValue::Null) : QJsonValue(number(terminal))},
                {"complete", counters.at(stream.key).accepted > 0 && !incomplete},
                {"dropped_samples", incomplete ? QJsonValue(QJsonValue::Null) : QJsonValue("0")},
                {"unknown_gap", unknownGap}});
        }
        auto result = client.request("capture_finalize", {{"handle", handle}, {"streams", summaries}}, 30000);
        if (!result.ok) { fail(result.error); return; }
        const auto savedStreams = result.result.value("streams").toArray();
        std::map<StreamKey, StreamCounters> finalCounters;
        for (const auto& entry : savedStreams) {
            const auto saved = entry.toObject();
            const auto identity = saved.value("stream").toObject();
            const auto stream = std::find_if(capture.streams.begin(), capture.streams.end(), [&](const auto& candidate) {
                return acquisition_io::identityToJson(device, candidate) == identity;
            });
            StreamCounters values;
            if (stream == capture.streams.end() || !parseCounters(saved, values) ||
                !finalCounters.emplace(stream->key, values).second) {
                fail(makeError("ipc.invalid_frame", {{"reason", "invalid capture stream summary"}})); return;
            }
        }
        if (finalCounters.size() != capture.streams.size()) {
            fail(makeError("ipc.invalid_frame", {{"reason", "incomplete capture stream summaries"}})); return;
        }
        const auto total = aggregateCounters(finalCounters);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->snapshot.acceptedSamples = total.accepted;
            state->snapshot.writtenSamples = total.written;
            state->snapshot.flushedSamples = total.flushed;
            state->terminal = overflow ? CaptureRecordState::Failed :
                result.result.value("status").toString() == "complete" ? CaptureRecordState::Saved : CaptureRecordState::Partial;
            if (overflow) state->snapshot.error = makeError("recorder.queue_full");
            state->consumer.reset();
        }
    });
    connect(worker_, &QThread::finished, this, [this] {
        // finished is emitted before all thread cleanup completes. Make an
        // immediate start() from our signals safe to delete/reuse worker_.
        worker_->wait();
        { std::lock_guard<std::mutex> lock(state_->mutex);
          if (state_->terminal) state_->snapshot.state = *state_->terminal;
          state_->terminal.reset(); state_->active = false; }
        // A terminal stateChanged handler may immediately start a new run;
        // deliver the old completion first so it cannot finish that new run.
        const QPointer<CaptureRecorder> owner(this);
        emit finished();
        if (owner) emit stateChanged();
    });
    worker_->start();
    return true;
}
} // namespace bf
