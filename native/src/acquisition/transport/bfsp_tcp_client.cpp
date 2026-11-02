#include "bfsp_tcp_client.h"

#include "../protocol/codec.h"

#include <QElapsedTimer>
#include <QHash>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQueue>
#include <QScopedValueRollback>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#ifdef BFSP_ENABLE_QT_SERIALPORT
#include <QSerialPort>
#endif

#include <algorithm>
#include <limits>
#include <optional>

namespace bf::transport {
namespace {

using bfsp::MessageType;
using State = BfspTcpClient::State;

QString newUuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower(); }

bool validUuid(const QString& value)
{
    const QUuid parsed(value);
    return !parsed.isNull() && parsed.toString(QUuid::WithoutBraces).toLower() == value;
}

bool isMutation(const QString& method)
{
    return method != "device.get_info" && method != "device.get_status" &&
           method != "time.sync" && method != "config.validate";
}

int dtypeCode(const QString& name)
{
    if (name == "int16") return 1;
    if (name == "int24") return 2;
    if (name == "int32") return 3;
    if (name == "float32") return 4;
    if (name == "float64") return 5;
    return 0;
}

}  // namespace

class BfspTcpClient::Impl {
public:
    explicit Impl(BfspTcpClient* owner) : q(owner) {}
    ~Impl() { teardown(); }

    struct Pending {
        QString method;
        QJsonObject params;
        qint64 deadline = 0;
        State previous = State::Ready;
        bool recovery = false;
    };
    struct Descriptor {
        quint32 revision = 0;
        quint32 channels = 0;
        quint32 maxSamples = 0;
        quint32 clockId = 0;
        quint32 rateNum = 0;
        quint32 rateDen = 0;
        int dtype = 0;
        quint64 nextSample = 0;
        bool receivedData = false;
    };

    bool current(quint64 gen) const noexcept
    {
        return gen == generation && deviceIo && state != State::Disconnected && state != State::Faulted;
    }

    // ceil(2 * block_samples * 1000 * rate_den / rate_num). Reject periods
    // outside the remaining monotonic-clock range instead of shortening them.
    // Split the division before multiplying so maximum u32 inputs cannot overflow.
    static std::optional<quint64> twoBlockMillis(const Descriptor& d, quint64 cap)
    {
        const quint64 numerator = quint64(2000) * d.maxSamples;
        const quint64 whole = numerator / d.rateNum;
        const quint64 remainder = numerator % d.rateNum;
        if (whole > cap / d.rateDen) return std::nullopt;
        const quint64 base = whole * d.rateDen;
        const quint64 fraction = remainder * d.rateDen;
        const quint64 rounded = fraction / d.rateNum + (fraction % d.rateNum != 0);
        if (rounded > cap - base) return std::nullopt;
        return base + rounded;
    }

    bool armFirstDataDeadline(const QString& id, const Pending& operation)
    {
        // A recovered start needs the same DATA budget as an on-time response.
        // Do not restart the budget when reconciling acquisition.no_data itself.
        if (firstDataReceived || firstStartId == id) return true;
        const qint64 now = clock.elapsed();
        const quint64 cap = quint64(std::numeric_limits<qint64>::max() - now);
        quint64 waitMs = 5000;
        for (const auto& descriptor : descriptors) {
            if (!descriptor.rateNum || !descriptor.rateDen || !descriptor.maxSamples) {
                fail("protocol.invalid_payload", {{"reason", "invalid_block_period"}}); return false;
            }
            const auto period = twoBlockMillis(descriptor, cap);
            if (!period) {
                fail("capability.limit", {{"resource", "first_data_deadline"}}); return false;
            }
            waitMs = std::max(waitMs, *period);
        }
        if (waitMs > cap) {
            fail("capability.limit", {{"resource", "first_data_deadline"}}); return false;
        }
        firstDataDeadline = now + static_cast<qint64>(waitMs);
        firstStartId = id;
        firstStartOperation = operation;
        return true;
    }

    void teardown()
    {
        if (timer) {
            timer->stop();
            timer->deleteLater();
            timer = nullptr;
        }
        if (deviceIo) {
            QObject::disconnect(deviceIo, nullptr, q, nullptr);
            if (auto* tcp = qobject_cast<QTcpSocket*>(deviceIo)) tcp->abort();
            else deviceIo->close();
            deviceIo->deleteLater();
            deviceIo = nullptr;
        }
        pending.clear();
        outgoing.clear();
        outgoingBytes = 0;
        outgoingOffset = 0;
        input.clear();
        deliveryBytes.clear();
        outstandingBytes = 0;
        scheduled = false;
        parser.reset();
    }

    void setState(State next)
    {
        if (state == next) return;
        state = next;
        emit q->stateChanged(generation, state);
    }

    void fail(const QString& code, QJsonObject params = {})
    {
        if (state == State::Faulted || state == State::Disconnected) return;
        const auto gen = generation;
        emit q->errorOccurred(gen, code, params);
        if (generation != gen || state == State::Disconnected || state == State::Faulted) return;
        setState(State::Faulted);
        if (generation == gen && state == State::Faulted) teardown();
    }

    void setClientIdentity(const QString& name, const QString& version)
    {
        if ((state != State::Disconnected && state != State::Faulted) || name.trimmed().isEmpty() ||
            version.trimmed().isEmpty() || name.toUtf8().size() > 4096 || version.toUtf8().size() > 4096) {
            emit q->errorOccurred(generation, "protocol.invalid_payload", {{"field", "client_identity"}});
            return;
        }
        clientName = name;
        clientVersion = version;
    }

    bool initialize(quint64 gen, const QString& expected, qint64 frameTimeout)
    {
        if (hasGeneration && gen <= generation) return false;
        teardown();
        generation = gen;
        hasGeneration = true;
        state = State::Disconnected;
        txSequence = 0;
        rxSequence.clear();
        seenStreamIds.clear();
        descriptors.clear();
        endedStreams.clear();
        endSummaries.clear();
        expectedStreams.clear();
        usedIds.clear();
        usedOrder.clear();
        activeCapture.clear();
        sessionId.clear();
        unknownId.clear();
        unknownMethod.clear();
        lateResponse.reset();
        recoveryDeadline = nextRecoveryPoll = 0;
        accepted = false;
        firstDataDeadline = 0;
        firstStartId.clear();
        firstDataReceived = false;
        lastActive = 0;
        heartbeatMs = 1000;
        idleMs = 5000;
        expectedDevice = expected;
        peerLimits = {};
        peerLimits.max_channels = 256;
        clock.start();
        bfsp::ParserOptions options;
        options.limits.max_channels = 256;
        options.frame_timeout_ms = frameTimeout;
        parser = std::make_unique<bfsp::StreamParser>(options);
        timer = new QTimer(q);
        timer->setInterval(100);
        QObject::connect(timer, &QTimer::timeout, q, [this, gen] { if (gen == generation) tick(); });
        setState(State::Connecting);
        if (generation != gen || state != State::Connecting || !timer) return false;
        connectDeadline = clock.elapsed() + 5000;
        timer->start();
        return true;
    }

    void connectSignals(quint64 gen)
    {
        QObject::connect(deviceIo, &QIODevice::readyRead, q, [this, gen] {
            if (gen == generation) drainInput();
        });
        QObject::connect(deviceIo, &QIODevice::bytesWritten, q, [this, gen](qint64) {
            if (gen == generation) drainOutput();
        });
    }

    void beginHandshake()
    {
        const auto gen = generation;
        setState(State::Handshaking);
        if (!current(gen)) return;
        handshakeDeadline = clock.elapsed() + 5000;
        nonce = newUuid();
        const QJsonObject hello{{"protocol", QJsonObject{{"major", 1}, {"minor", 0}}},
            {"client_name", clientName}, {"client_version", clientVersion},
            {"receive_limits", QJsonObject{{"max_payload", int(bfsp::kMaxPayload)},
                {"max_json", int(bfsp::kMaxJson)}, {"max_channels", 256}, {"max_streams", 16}}},
            {"features", QJsonArray{"regular_samples.v1", "events.v1"}},
            {"required_features", QJsonArray{"regular_samples.v1"}}, {"nonce", nonce}};
        sendJson(MessageType::Hello, hello);
    }

    void open(quint64 gen, const QString& host, quint16 port, const QString& expected)
    {
        if (!initialize(gen, expected, 3000)) return;
        serialBaud = 0;
        if (host.isEmpty() || !port) { fail("device.invalid_endpoint"); return; }
        auto* tcp = new QTcpSocket(q);
        deviceIo = tcp;
        tcp->setReadBufferSize(128 * 1024);
        connectSignals(gen);
        QObject::connect(tcp, &QTcpSocket::connected, q, [this, gen, tcp] {
            if (gen != generation || deviceIo != tcp) return;
            tcp->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            beginHandshake();
        });
        QObject::connect(tcp, &QTcpSocket::disconnected, q, [this, gen] {
            if (gen == generation && state != State::Disconnected && state != State::Faulted)
                fail("device.disconnected");
        });
        QObject::connect(tcp, &QTcpSocket::errorOccurred, q, [this, gen](QAbstractSocket::SocketError) {
            if (gen == generation && state != State::Disconnected && state != State::Faulted)
                fail("device.disconnected");
        });
        tcp->connectToHost(host, port);
    }

#ifdef BFSP_ENABLE_QT_SERIALPORT
    void openSerial(quint64 gen, const QString& portName, qint32 baud, int dataBits,
                    int parity, int stopBits, int flowControl, const QString& expected)
    {
        if (!initialize(gen, expected, 30000)) return;
        if (portName.isEmpty() || baud <= 0 || dataBits < 5 || dataBits > 8 ||
            (parity != 0 && parity != 2 && parity != 3 && parity != 4 && parity != 5) ||
            (stopBits != 1 && stopBits != 2 && stopBits != 3) || flowControl < 0 || flowControl > 2) {
            fail("device.invalid_endpoint"); return;
        }
        serialBaud = baud;
        serialBitsTenths = 10 + dataBits * 10 + (parity ? 10 : 0) + (stopBits == 3 ? 15 : stopBits * 10);
        auto* serial = new QSerialPort(q);
        deviceIo = serial;
        serial->setReadBufferSize(128 * 1024);
        serial->setPortName(portName);
        if (!serial->setBaudRate(baud) || !serial->setDataBits(QSerialPort::DataBits(dataBits)) ||
            !serial->setParity(QSerialPort::Parity(parity)) || !serial->setStopBits(QSerialPort::StopBits(stopBits)) ||
            !serial->setFlowControl(QSerialPort::FlowControl(flowControl)) || !serial->open(QIODevice::ReadWrite)) {
            fail("device.disconnected"); return;
        }
        connectSignals(gen);
        QObject::connect(serial, &QSerialPort::errorOccurred, q, [this, gen](QSerialPort::SerialPortError error) {
            if (gen == generation && error != QSerialPort::NoError && state != State::Faulted)
                fail("device.disconnected");
        });
        beginHandshake();
    }
#endif

    void close(quint64 gen)
    {
        if (hasGeneration && gen <= generation) return;
        teardown();
        generation = gen;
        hasGeneration = true;
        setState(State::Disconnected);
    }

    qint64 frameTimeMs(qint64 bytes) const
    {
        if (!serialBaud) return 0;
        const qint64 denominator = qint64(serialBaud) * 10;
        return (bytes * serialBitsTenths * 1000 + denominator - 1) / denominator;
    }

    bool sendJson(MessageType type, const QJsonObject& object)
    {
        const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QByteArray frame;
        auto result = bfsp::encode_frame(type, 0, txSequence, payload, frame, peerLimits);
        if (!result) {
            fail(QString::fromLatin1(bfsp::error_code(result.error)));
            return false;
        }
        if (serialBaud && frameTimeMs(frame.size()) > idleMs / 2) {
            fail("capability.limit", {{"resource", "serial_control_time"}}); return false;
        }
        constexpr qint64 limit = 2 * 1024 * 1024;
        if (!deviceIo || outgoing.size() >= 128 || outgoingBytes + deviceIo->bytesToWrite() + frame.size() > limit) {
            fail("protocol.limit_exceeded", {{"resource", "write_queue"}});
            return false;
        }
        ++txSequence;
        outgoingBytes += frame.size();
        outgoing.enqueue(std::move(frame));
        drainOutput();
        return state != State::Faulted;
    }

    void drainOutput()
    {
        if (!deviceIo || !deviceIo->isOpen()) return;
        constexpr qint64 window = 64 * 1024;
        while (!outgoing.isEmpty() && deviceIo->bytesToWrite() < window) {
            const auto& frame = outgoing.head();
            const qint64 amount = std::min<qint64>(frame.size() - outgoingOffset, window - deviceIo->bytesToWrite());
            const qint64 written = deviceIo->write(frame.constData() + outgoingOffset, amount);
            if (written < 0) { fail("device.disconnected"); return; }
            if (!written) break;
            outgoingOffset += written;
            outgoingBytes -= written;
            if (outgoingOffset == frame.size()) { outgoing.dequeue(); outgoingOffset = 0; }
        }
    }

    void scheduleDrain()
    {
        if (scheduled || !deviceIo) return;
        scheduled = true;
        const auto gen = generation;
        QTimer::singleShot(0, q, [this, gen] {
            if (gen != generation) return;
            scheduled = false;
            drainInput();
        });
    }

    void drainInput()
    {
        if (!deviceIo || !parser || drainingInput) return;
        // Direct consumers may run a nested event loop. Never feed the parser
        // again before this batch's verified frames and diagnostics are drained.
        QScopedValueRollback<bool> draining(drainingInput, true);
        const auto gen = generation;
        auto* const io = deviceIo;
        auto* const activeParser = parser.get();
        const auto active = [&] { return current(gen) && deviceIo == io && parser.get() == activeParser; };
        // Bounded work per event-loop turn; the parser retains partial frames.
        for (int iteration = 0; iteration < 4 && active(); ++iteration) {
            if (input.isEmpty() && deviceIo->bytesAvailable()) input = deviceIo->read(64 * 1024);
            const auto result = parser->feed(QByteArrayView(input), clock.elapsed());
            input.remove(0, result.consumed);
            for (const auto& frame : result.frames) {
                lastActive = clock.elapsed();
                if (!handleFrame(frame)) return;
                if (!active()) return;
            }
            for (const auto& error : result.errors) {
                emit q->errorOccurred(gen, QString::fromLatin1(bfsp::error_code(error.error)), {});
                if (!active()) return;
            }
            if (result.faulted) { fail("protocol.invalid_frame"); return; }
            if (input.isEmpty() && !deviceIo->bytesAvailable() && result.frames.empty()) return;
            if (!input.isEmpty() && result.consumed == 0 && result.frames.empty()) {
                fail("protocol.limit_exceeded", {{"resource", "read_queue"}});
                return;
            }
        }
        if (active() && (!input.isEmpty() || deviceIo->bytesAvailable() || parser->buffered_bytes())) scheduleDrain();
    }

    bool checkSequence(const bfsp::Header& header)
    {
        const auto gen = generation;
        const quint32 expected = rxSequence.value(header.stream_id, 0);
        const quint32 delta = header.sequence - expected;
        if (delta >= 0x80000000u) { fail("protocol.invalid_frame", {{"reason", "sequence"}}); return false; }
        if (delta) emit q->errorOccurred(generation, "protocol.sequence_gap", {{"stream_id", double(header.stream_id)}});
        if (!current(gen)) return false;
        rxSequence[header.stream_id] = header.sequence + 1;
        return true;
    }

    bool handleFrame(const bfsp::Frame& frame)
    {
        const auto gen = generation;
        if (serialBaud) {
            const auto maxMs = frame.header.type == MessageType::Data ? 250 : idleMs / 2;
            if (frameTimeMs(qint64(frame.header.payload_length) + 28) > maxMs) {
                fail("capability.limit", {{"resource", "serial_frame_time"}}); return false;
            }
        }
        if (!checkSequence(frame.header)) return false;
        const auto type = frame.header.type;
        if (state == State::Handshaking) {
            if (type != MessageType::HelloAck) { fail("protocol.invalid_payload"); return false; }
            return handleHello(frame.payload);
        }
        if (state == State::Connecting || state == State::Disconnected || state == State::Faulted ||
            type == MessageType::Hello || type == MessageType::HelloAck || type == MessageType::Command) {
            fail("protocol.invalid_payload"); return false;
        }
        if (type == MessageType::Data) return handleData(frame);
        const QJsonObject object = QJsonDocument::fromJson(frame.payload).object();
        if (type == MessageType::Heartbeat) return true;
        if (type == MessageType::Response) return handleResponse(object);
        if (type == MessageType::Descriptor) return handleDescriptor(frame.header.stream_id, object);
        if (type == MessageType::Status) {
            if (state == State::OutcomeUnknown)
                return handleRecovery({{"ok", true}, {"result", object}});
            emit q->statusReceived(generation, object); return true;
        }
        if (type == MessageType::Event) { emit q->eventReceived(generation, frame.header.stream_id, object); return true; }
        if (type == MessageType::StreamEnd) {
            const auto stream = frame.header.stream_id;
            if (object.value("capture_id").toString() != activeCapture || !descriptors.contains(stream) ||
                endedStreams.contains(stream)) {
                fail("protocol.invalid_payload"); return false;
            }
            const auto endValue = object.value("last_sample_exclusive");
            if (!endValue.isNull()) {
                bool valid = false;
                const quint64 end = endValue.toString().toULongLong(&valid);
                const quint64 next = descriptors.value(stream).nextSample;
                if (!valid || end < next) { fail("protocol.invalid_payload", {{"reason", "invalid_stream_end"}}); return false; }
                if (end > next) emit q->errorOccurred(generation, "acquisition.data_gap", {
                    {"stream_id", double(stream)}, {"start", QString::number(next)}, {"count", QString::number(end - next)}});
                if (!current(gen)) return false;
            }
            endedStreams.insert(stream);
            endSummaries.insert(stream, object);
            emit q->streamEnded(generation, stream, object);
            return true;
        }
        fail("protocol.invalid_payload");
        return false;
    }

    bool handleHello(const QByteArray& payload)
    {
        const auto gen = generation;
        const auto ack = QJsonDocument::fromJson(payload).object();
        if (ack.value("nonce").toString() != nonce) { fail("protocol.invalid_payload"); return false; }
        if (!ack.value("ok").toBool()) {
            fail(ack.value("error").toObject().value("code").toString("protocol.invalid_payload"));
            return false;
        }
        const auto device = ack.value("device").toObject();
        if (!expectedDevice.isEmpty() && device.value("device_id").toString() != expectedDevice) {
            fail("device.identity_mismatch"); return false;
        }
        if (!ack.value("features").toArray().contains("regular_samples.v1")) {
            fail("capability.unsupported"); return false;
        }
        heartbeatMs = ack.value("heartbeat_ms").toInt();
        idleMs = ack.value("idle_timeout_ms").toInt();
        if (heartbeatMs <= 0 || idleMs > 30000 || heartbeatMs > idleMs / 3) {
            fail("protocol.invalid_payload"); return false;
        }
        const auto limits = ack.value("receive_limits").toObject();
        peerLimits.max_payload = static_cast<quint32>(limits.value("max_payload").toDouble());
        peerLimits.max_json = static_cast<quint32>(limits.value("max_json").toDouble());
        peerLimits.max_channels = static_cast<quint32>(limits.value("max_channels").toDouble());
        sessionId = ack.value("session_id").toString();
        nextHeartbeat = clock.elapsed() + heartbeatMs;
        lastActive = clock.elapsed();
        setState(State::Ready);
        if (!current(gen)) return false;
        emit q->connected(generation, ack);
        return true;
    }

    bool handleDescriptor(quint32 stream, const QJsonObject& object)
    {
        // A prepare response may be delayed or lost while its descriptor arrives.
        // Keep the provisional capture isolated until the response/status confirms it.
        if (activeCapture.isEmpty()) {
            bool preparing = unknownMethod == "capture.prepare" && !unknownId.isEmpty();
            for (auto it = pending.cbegin(); it != pending.cend(); ++it) preparing |= it->method == "capture.prepare";
            if (preparing) activeCapture = object.value("capture_id").toString();
        }
        const bool provisional = expectedStreams.isEmpty() && !activeCapture.isEmpty();
        if (object.value("session_id").toString() != sessionId ||
            object.value("capture_id").toString() != activeCapture ||
            (!provisional && !expectedStreams.contains(stream)) || (provisional && seenStreamIds.contains(stream)) ||
            descriptors.contains(stream) || descriptors.size() >= 16) {
            fail("protocol.invalid_payload", {{"reason", "unexpected_descriptor"}}); return false;
        }
        Descriptor descriptor;
        descriptor.revision = static_cast<quint32>(object.value("descriptor_revision").toDouble());
        descriptor.channels = object.value("channels").toArray().size();
        descriptor.maxSamples = static_cast<quint32>(object.value("max_samples_per_block").toDouble());
        descriptor.clockId = static_cast<quint32>(object.value("clock").toObject().value("clock_id").toDouble());
        descriptor.rateNum = static_cast<quint32>(object.value("rate_num").toDouble());
        descriptor.rateDen = static_cast<quint32>(object.value("rate_den").toDouble());
        descriptor.dtype = dtypeCode(object.value("dtype").toString());
        descriptors.insert(stream, descriptor);
        emit q->descriptorReceived(generation, stream, object);
        return true;
    }

    bool handleData(const bfsp::Frame& frame)
    {
        const auto gen = generation;
        const auto stream = frame.header.stream_id;
        if (!accepted || !descriptors.contains(stream) || endedStreams.contains(stream) ||
            (state != State::Starting && state != State::Streaming && state != State::Stopping && state != State::OutcomeUnknown)) {
            fail("protocol.invalid_payload", {{"reason", "unexpected_data"}}); return false;
        }
        bfsp::DataPrefix prefix;
        const auto result = bfsp::decode_data(frame.payload, prefix);
        auto& descriptor = descriptors[stream];
        if (!result || prefix.descriptor_revision != descriptor.revision || prefix.channel_count != descriptor.channels ||
            int(prefix.dtype) != descriptor.dtype || prefix.clock_id != descriptor.clockId ||
            prefix.sample_count > descriptor.maxSamples || prefix.first_sample_index < descriptor.nextSample ||
            prefix.first_sample_index > std::numeric_limits<quint64>::max() - prefix.sample_count) {
            fail("protocol.invalid_payload", {{"reason", "data_descriptor_mismatch"}}); return false;
        }
        if (prefix.first_sample_index > descriptor.nextSample) {
            emit q->errorOccurred(generation, "acquisition.data_gap", {{"stream_id", double(stream)},
                {"start", QString::number(descriptor.nextSample)},
                {"count", QString::number(prefix.first_sample_index - descriptor.nextSample)}});
            if (!current(gen)) return false;
        }
        constexpr qint64 maxDeliveryBytes = 8 * 1024 * 1024;
        if (deliveryBytes.size() >= 128 || outstandingBytes + frame.payload.size() > maxDeliveryBytes) {
            fail("capture.backlog_overflow", {{"resource", "data_delivery"}}); return false;
        }
        const auto key = deliveryKey(activeCapture, stream, prefix.first_sample_index);
        deliveryBytes.insert(key, frame.payload.size());
        outstandingBytes += frame.payload.size();
        descriptor.nextSample = prefix.first_sample_index + prefix.sample_count;
        descriptor.receivedData = true;
        firstDataReceived = true;
        firstDataDeadline = 0;
        if (state == State::Starting) setState(State::Streaming);
        if (!current(gen)) return false;
        const QJsonObject metadata{{"capture_id", activeCapture}, {"descriptor_revision", double(prefix.descriptor_revision)},
            {"dtype", int(prefix.dtype)}, {"time_flags", int(prefix.time_flags)},
            {"channel_count", double(prefix.channel_count)}, {"sample_count", double(prefix.sample_count)},
            {"first_sample_index", QString::number(prefix.first_sample_index)},
            {"first_timestamp_ticks", QString::number(prefix.first_timestamp_ticks)},
            {"clock_id", double(prefix.clock_id)}, {"quality_flags", double(prefix.quality_flags)}};
        emit q->dataReceived(generation, stream, metadata, frame.payload);
        return true;
    }

    static QString deliveryKey(const QString& capture, quint32 stream, quint64 sample)
    { return capture + QLatin1Char(':') + QString::number(stream) + QLatin1Char(':') + QString::number(sample); }

    void acknowledgeData(quint64 gen, const QString& capture, quint32 stream, quint64 sample)
    {
        if (gen != generation) return;
        const auto it = deliveryBytes.find(deliveryKey(capture, stream, sample));
        if (it == deliveryBytes.end()) return;
        outstandingBytes -= it.value();
        deliveryBytes.erase(it);
    }

    bool handleResponse(const QJsonObject& response)
    {
        const auto gen = generation;
        const QString id = response.value("request_id").toString();
        if (!pending.contains(id)) {
            if (id == unknownId) {
                if (lateResponse && *lateResponse != response) {
                    fail("protocol.invalid_payload", {{"reason", "conflicting_late_response"}});
                    return false;
                }
                lateResponse = response;
            }
            return true;
        }
        const Pending operation = pending.take(id);
        if (operation.recovery) return handleRecovery(response);
        if (operation.method == "device.get_status" && state == State::OutcomeUnknown) {
            if (!handleRecovery(response) || !current(gen)) return false;
            emit q->responseReceived(gen, response);
            return current(gen);
        }
        const bool ok = response.value("ok").toBool();
        const auto result = response.value("result").toObject();
        if (operation.method == "capture.prepare" && ok) {
            if (!adoptPrepared(result)) return false;
            setState(State::Prepared);
        } else if (operation.method == "capture.prepare" && !ok) {
            if (!descriptors.isEmpty()) { fail("protocol.invalid_payload", {{"reason", "prepare_conflict"}}); return false; }
            clearCapture();
        } else if (operation.method == "stream.accept" && ok) {
            if (!result.value("accepted").toBool()) { fail("protocol.invalid_payload"); return false; }
            accepted = true;
        } else if (operation.method == "capture.start") {
            if (ok && (result.value("state").toString() != "Streaming" || result.value("capture_id").toString() != activeCapture)) {
                fail("protocol.invalid_payload"); return false;
            }
            if (ok && state == State::Starting && !armFirstDataDeadline(id, operation)) return false;
            if (!ok && state == State::Streaming) {
                enterUnknown(id, operation);
            } else if (!ok && state == State::Starting) setState(State::Prepared);
        } else if (operation.method == "capture.stop") {
            if (ok) {
                if (result.value("state").toString() != "Idle" || result.value("capture_id").toString() != activeCapture ||
                    endedStreams != expectedStreams) { fail("protocol.invalid_payload", {{"reason", "missing_end"}}); return false; }
                const auto summaries = result.value("streams").toArray();
                if (summaries.size() != endSummaries.size()) { fail("protocol.invalid_payload"); return false; }
                QSet<quint32> seen;
                for (const auto& value : summaries) {
                    const auto summary = value.toObject();
                    const auto stream = quint32(summary.value("stream_id").toDouble());
                    if (seen.contains(stream) || !endSummaries.contains(stream) || endSummaries.value(stream) != summary) {
                        fail("protocol.invalid_payload", {{"reason", "end_summary_mismatch"}}); return false;
                    }
                    seen.insert(stream);
                }
                // A stop may preempt an in-flight start. Once stop and all ENDs
                // are confirmed, a late start response cannot revive the capture.
                const auto ids = pending.keys();
                for (const auto& pendingId : ids) {
                    const auto previous = pending.value(pendingId);
                    if (previous.method == "capture.start" && previous.params.value("capture_id").toString() == activeCapture)
                        pending.remove(pendingId);
                }
                clearCapture();
                setState(State::Ready);
            } else if (state == State::Stopping) setState(operation.previous);
        } else if (operation.method == "capture.discard" && ok) {
            clearCapture();
            setState(State::Ready);
        }
        if (!current(gen)) return false;
        emit q->responseReceived(gen, response);
        return state != State::Faulted;
    }

    void clearCapture()
    {
        for (const auto stream : expectedStreams) rxSequence.remove(stream);
        activeCapture.clear();
        descriptors.clear();
        expectedStreams.clear();
        endedStreams.clear();
        endSummaries.clear();
        accepted = false;
        firstDataDeadline = 0;
        firstDataReceived = false;
        firstStartId.clear();
    }

    bool adoptPrepared(const QJsonObject& result)
    {
        const QString capture = result.value("capture_id").toString();
        const auto streams = result.value("stream_ids").toArray();
        if (!validUuid(capture) || (!activeCapture.isEmpty() && capture != activeCapture) ||
            streams.isEmpty() || streams.size() > 16) { fail("protocol.invalid_payload"); return false; }
        QSet<quint32> announced;
        for (const auto& value : streams) {
            const auto number = value.toDouble();
            if (!value.isDouble() || number < 1 || number > 4294967295.0 || number != quint32(number) ||
                announced.contains(quint32(number))) { fail("protocol.invalid_payload"); return false; }
            announced.insert(quint32(number));
        }
        constexpr qsizetype maxSessionStreams = 4096;
        if (seenStreamIds.size() + announced.size() > maxSessionStreams) {
            fail("capability.limit", {{"resource", "session_stream_ids"}, {"action", "reconnect"}}); return false;
        }
        for (const auto stream : announced) {
            if (seenStreamIds.contains(stream)) {
                fail("protocol.invalid_payload", {{"reason", "stream_id_reused"}}); return false;
            }
        }
        for (auto it = descriptors.cbegin(); it != descriptors.cend(); ++it) {
            if (!announced.contains(it.key())) { fail("protocol.invalid_payload"); return false; }
        }
        activeCapture = capture;
        expectedStreams = announced;
        seenStreamIds.unite(announced);
        accepted = false;
        return true;
    }

    bool handleRecovery(const QJsonObject& response)
    {
        const auto gen = generation;
        if (clock.elapsed() >= recoveryDeadline) {
            fail("request.timeout", {{"method", "device.get_status"}, {"reason", "recovery_deadline"}}); return false;
        }
        if (!response.value("ok").toBool()) { fail("request.timeout", {{"method", "device.get_status"}}); return false; }
        const auto status = response.value("result").toObject();
        const auto last = status.value("last_operation").toObject();
        const QString operationState = last.value("state").toString();
        if (last.value("request_id").toString() != unknownId ||
            (operationState != "completed" && operationState != "failed")) {
            const auto now = clock.elapsed();
            nextRecoveryPoll = recoveryDeadline - now <= 1000 ? recoveryDeadline : now + 1000;
            emit q->statusReceived(gen, status);
            return current(gen);
        }
        if (last.value("method").toString() != unknownMethod) {
            fail("protocol.invalid_payload", {{"reason", "recovery_method_mismatch"}}); return false;
        }
        if (lateResponse) {
            const bool completed = operationState == "completed";
            const auto field = completed ? "result" : "error";
            if (lateResponse->value("ok").toBool() != completed ||
                lateResponse->value(field) != last.value(field)) {
                fail("protocol.invalid_payload", {{"reason", "recovery_response_conflict"}}); return false;
            }
        }
        const QString observed = status.value("state").toString();
        const QString capture = status.value("active_capture_id").toString();
        if (unknownMethod == "capture.prepare" && operationState == "completed" &&
            !adoptPrepared(last.value("result").toObject())) return false;
        if (unknownMethod == "capture.prepare" && operationState == "failed" && !descriptors.isEmpty()) {
            fail("protocol.invalid_payload", {{"reason", "prepare_conflict"}}); return false;
        }
        if ((observed == "Prepared" || observed == "Starting" || observed == "Streaming" || observed == "Stopping") &&
            capture != activeCapture) { fail("protocol.invalid_payload", {{"reason", "capture_mismatch"}}); return false; }
        if (unknownMethod == "capture.start" && operationState == "failed" && observed == "Streaming") {
            fail("device.invalid_state", {{"reason", "start_conflict"}}); return false;
        }
        if (unknownMethod == "capture.start" && operationState == "completed") {
            const auto result = last.value("result").toObject();
            if (result.value("state").toString() != "Streaming" || result.value("capture_id").toString() != activeCapture) {
                fail("protocol.invalid_payload"); return false;
            }
        }
        if (unknownMethod == "stream.accept" && operationState == "completed") {
            accepted = last.value("result").toObject().value("accepted").toBool();
            if (!accepted) { fail("protocol.invalid_payload"); return false; }
        }
        State restored;
        const bool missingEnd = observed == "Idle" && unknownMethod == "capture.stop" && endedStreams != expectedStreams;
        if (observed == "Idle") {
            clearCapture(); restored = State::Ready;
        }
        else if (observed == "Prepared") restored = State::Prepared;
        else if (observed == "Streaming") restored = firstDataReceived ? State::Streaming : State::Starting;
        else if (observed == "Starting") restored = State::Starting;
        else if (observed == "Stopping") restored = State::Stopping;
        else { fail("device.invalid_state"); return false; }
        if (unknownMethod == "capture.start" && operationState == "completed" && restored == State::Starting &&
            !armFirstDataDeadline(unknownId, unknownOperation)) return false;
        // Commit all transport bookkeeping before any direct Source callback
        // can issue a new mutation, close/reopen, or run a nested event loop.
        unknownId.clear();
        unknownMethod.clear();
        lateResponse.reset();
        recoveryDeadline = nextRecoveryPoll = 0;
        const auto ids = pending.keys();
        for (const auto& id : ids) if (pending.value(id).recovery) pending.remove(id);
        setState(restored);
        if (!current(gen)) return false;
        if (missingEnd) {
            emit q->errorOccurred(gen, "acquisition.stream_end_missing", {});
            if (!current(gen)) return false;
        }
        emit q->statusReceived(gen, status);
        return current(gen);
    }

    void command(quint64 gen, const QString& id, const QString& method, const QJsonObject& params)
    {
        if (gen != generation || !deviceIo) return;
        if (!validUuid(id) || method.isEmpty() || usedIds.contains(id) || pending.size() >= 64) {
            emit q->errorOccurred(gen, "protocol.invalid_payload", {{"request_id", id}}); return;
        }
        if (state == State::Connecting || state == State::Handshaking || state == State::Faulted || state == State::Disconnected ||
            (state == State::OutcomeUnknown && method != "device.get_status")) {
            emit q->errorOccurred(gen, "device.invalid_state", {{"method", method}}); return;
        }
        if (isMutation(method)) {
            for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
                if (isMutation(it->method) && !(method == "capture.stop" && it->method == "capture.start")) {
                    emit q->errorOccurred(gen, "device.busy", {{"method", method}}); return;
                }
            }
        }
        if ((method == "capture.prepare" || method == "config.apply" || method == "config.validate") && state != State::Ready) {
            emit q->errorOccurred(gen, "device.invalid_state", {{"method", method}}); return;
        }
        if (method == "stream.accept") {
            if (state != State::Prepared || params.value("capture_id").toString() != activeCapture ||
                descriptors.size() != expectedStreams.size()) {
                emit q->errorOccurred(gen, "device.invalid_state", {{"method", method}}); return;
            }
            const auto streams = params.value("streams").toArray();
            QSet<quint32> selected;
            for (const auto& value : streams) {
                const auto entry = value.toObject();
                const auto stream = quint32(entry.value("stream_id").toDouble());
                if (!descriptors.contains(stream) || selected.contains(stream) ||
                    entry.value("descriptor_revision").toDouble() != descriptors.value(stream).revision) {
                    emit q->errorOccurred(gen, "protocol.invalid_payload", {{"method", method}}); return;
                }
                selected.insert(stream);
            }
            if (selected != expectedStreams) { emit q->errorOccurred(gen, "protocol.invalid_payload", {{"method", method}}); return; }
        }
        if (method == "capture.start" && (state != State::Prepared || !accepted || params.value("capture_id").toString() != activeCapture)) {
            emit q->errorOccurred(gen, "device.invalid_state", {{"method", method}}); return;
        }
        if (method == "capture.stop" && ((state != State::Starting && state != State::Streaming) || params.value("capture_id").toString() != activeCapture)) {
            emit q->errorOccurred(gen, "device.invalid_state", {{"method", method}}); return;
        }
        if (method == "capture.discard" && (state != State::Prepared || params.value("capture_id").toString() != activeCapture)) {
            emit q->errorOccurred(gen, "device.invalid_state", {{"method", method}}); return;
        }
        sendRequest(id, method, params, false);
    }

    bool sendRequest(const QString& id, const QString& method, const QJsonObject& params, bool recovery)
    {
        const auto gen = generation;
        Pending operation{method, params, clock.elapsed() + (isMutation(method) ? 10000 : 3000), state, recovery};
        pending.insert(id, operation);
        usedIds.insert(id);
        usedOrder.enqueue(id);
        if (usedOrder.size() > 256) usedIds.remove(usedOrder.dequeue());
        if (!sendJson(MessageType::Command, {{"request_id", id}, {"method", method}, {"params", params}})) return false;
        if (!current(gen)) return false;
        if (method == "capture.start") setState(State::Starting);
        if (method == "capture.stop") setState(State::Stopping);
        return current(gen);
    }

    void enterUnknown(const QString& id, const Pending& operation)
    {
        if (!unknownId.isEmpty()) { fail("request.timeout"); return; }
        const auto gen = generation;
        unknownId = id;
        unknownMethod = operation.method;
        unknownOperation = operation;
        lateResponse.reset();
        const auto now = clock.elapsed();
        recoveryDeadline = now > std::numeric_limits<qint64>::max() - 10000
                ? std::numeric_limits<qint64>::max() : now + 10000;
        nextRecoveryPoll = now;
        setState(State::OutcomeUnknown);
        if (!current(gen)) return;
        sendRequest(newUuid(), "device.get_status", {}, true);
    }

    void tick()
    {
        if (!deviceIo || !parser) return;
        const auto gen = generation;
        const qint64 now = clock.elapsed();
        if (state == State::Connecting && now >= connectDeadline) { fail("request.timeout", {{"method", "connect"}}); return; }
        if (state == State::Handshaking && now >= handshakeDeadline) { fail("request.timeout", {{"method", "hello"}}); return; }
        if (state != State::Connecting && state != State::Handshaking) {
            if (now - lastActive >= idleMs) { fail("device.disconnected", {{"reason", "heartbeat_timeout"}}); return; }
            if (now >= nextHeartbeat) {
                if (!sendJson(MessageType::Heartbeat, {})) return;
                nextHeartbeat = now + heartbeatMs;
            }
        }
        if (state == State::Starting && firstDataDeadline && now >= firstDataDeadline) {
            firstDataDeadline = 0;
            emit q->errorOccurred(generation, "acquisition.no_data", {{"capture_id", activeCapture}});
            if (!current(gen) || state != State::Starting) return;
            enterUnknown(firstStartId, firstStartOperation);
            if (!current(gen)) return;
        }
        const auto ids = pending.keys();
        for (const auto& id : ids) {
            if (!pending.contains(id) || pending.value(id).deadline > now) continue;
            const auto operation = pending.take(id);
            emit q->errorOccurred(generation, "request.timeout", {{"request_id", id}, {"method", operation.method}});
            if (!current(gen)) return;
            if (operation.recovery) { fail("request.timeout", {{"method", "device.get_status"}}); return; }
            if (isMutation(operation.method)) {
                enterUnknown(id, operation);
                if (!current(gen)) return;
            }
        }
        if (state == State::OutcomeUnknown && !unknownId.isEmpty()) {
            if (now >= recoveryDeadline) {
                fail("request.timeout", {{"method", "device.get_status"}, {"reason", "recovery_deadline"}});
                return;
            }
            bool recoveryPending = false;
            for (auto it = pending.cbegin(); it != pending.cend(); ++it) recoveryPending |= it->recovery;
            if (!recoveryPending && now >= nextRecoveryPoll)
                if (!sendRequest(newUuid(), "device.get_status", {}, true)) return;
        }
        drainInput();
    }

    BfspTcpClient* q;
    QIODevice* deviceIo = nullptr;
    QTimer* timer = nullptr;
    std::unique_ptr<bfsp::StreamParser> parser;
    QElapsedTimer clock;
    State state = State::Disconnected;
    quint64 generation = 0;
    quint32 txSequence = 0;
    QHash<quint32, quint32> rxSequence;
    QHash<QString, Pending> pending;
    QSet<QString> usedIds;
    QQueue<QString> usedOrder;
    QHash<quint32, Descriptor> descriptors;
    QSet<quint32> expectedStreams, endedStreams, seenStreamIds;
    QHash<quint32, QJsonObject> endSummaries;
    QHash<QString, qsizetype> deliveryBytes;
    QQueue<QByteArray> outgoing;
    QByteArray input;
    qint64 outgoingBytes = 0, outgoingOffset = 0;
    qint64 outstandingBytes = 0;
    qint64 connectDeadline = 0, handshakeDeadline = 0, lastActive = 0, nextHeartbeat = 0;
    qint64 firstDataDeadline = 0;
    qint64 recoveryDeadline = 0, nextRecoveryPoll = 0;
    int heartbeatMs = 1000, idleMs = 5000;
    qint32 serialBaud = 0;
    int serialBitsTenths = 100;
    bool scheduled = false, drainingInput = false, accepted = false, firstDataReceived = false, hasGeneration = false;
    QString nonce, sessionId, activeCapture, expectedDevice, unknownId, unknownMethod, firstStartId;
    QString clientName = QStringLiteral("BoundFree Community"), clientVersion = QStringLiteral("unknown");
    Pending firstStartOperation;
    Pending unknownOperation;
    std::optional<QJsonObject> lateResponse;
    bfsp::Limits peerLimits;
};

BfspTcpClient::BfspTcpClient(QObject* parent) : QObject(parent), d(std::make_unique<Impl>(this))
{ qRegisterMetaType<BfspTcpClient::State>(); }
BfspTcpClient::~BfspTcpClient() = default;
void BfspTcpClient::setClientIdentity(const QString& clientName, const QString& clientVersion)
{ d->setClientIdentity(clientName, clientVersion); }
void BfspTcpClient::open(quint64 generation, const QString& host, quint16 port, const QString& expectedDeviceId)
{ d->open(generation, host, port, expectedDeviceId); }
#ifdef BFSP_ENABLE_QT_SERIALPORT
void BfspTcpClient::openSerial(quint64 generation, const QString& portName, qint32 baudRate,
                               int dataBits, int parity, int stopBits, int flowControl,
                               const QString& expectedDeviceId)
{ d->openSerial(generation, portName, baudRate, dataBits, parity, stopBits, flowControl, expectedDeviceId); }
#endif
void BfspTcpClient::close(quint64 generation) { d->close(generation); }
void BfspTcpClient::command(quint64 generation, const QString& requestId, const QString& method, const QJsonObject& params)
{ d->command(generation, requestId, method, params); }
void BfspTcpClient::acknowledgeData(quint64 generation, const QString& captureId, quint32 streamId, quint64 firstSampleIndex)
{ d->acknowledgeData(generation, captureId, streamId, firstSampleIndex); }

}  // namespace bf::transport
