#include "controller/acquisition_wifi_source.h"

#include "acquisition/adapters/rt_bci_decoder.h"
#include "controller/acquisition_view.h"
#include "controller/acquisition_diagnostic_log.h"
#include "core/app_error.h"

#include <QHostAddress>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <utility>

namespace bf {
namespace {
using namespace acquisition;
using Context = AcquisitionSource::CaptureContext;
constexpr qint64 MaxHttpBytes = 64 * 1024;
constexpr qint64 MaxPostHeaders = 16 * 1024;
constexpr qint64 MaxPostWire = MaxHttpBytes + 2 * MaxPostHeaders;
constexpr qint64 MaxSocketRead = 64 * 1024;
constexpr int HttpTimeoutMs = 10000;
constexpr int CallbackTimeoutMs = 3000;
constexpr int StopLimitMs = 10000;

bool ipv4(const QString& text, QHostAddress& result)
{
    if (!result.setAddress(text.trimmed()) || result.protocol() != QAbstractSocket::IPv4Protocol ||
        result.isNull() || result == QHostAddress::AnyIPv4 || result == QHostAddress::Broadcast ||
        result.isMulticast()) return false;
    return true;
}

QString routeAddress(const QHostAddress& target, const QString& requested = {})
{
    if (!requested.trimmed().isEmpty()) {
        QHostAddress local;
        if (!ipv4(requested, local)) return {};
        for (const auto& address : QNetworkInterface::allAddresses())
            if (address == local) return local.toString();
        return {};
    }
    // UDP connect selects the OS route without sending a datagram or scanning.
    QUdpSocket socket;
    socket.connectToHost(target, 9);
    const auto local = socket.localAddress();
    if (socket.state() != QAbstractSocket::ConnectedState ||
        local.protocol() != QAbstractSocket::IPv4Protocol || local == QHostAddress::AnyIPv4)
        return {};
    return local.toString();
}

struct HttpResult {
    bool ok = false;
    int status = 0;
    QByteArray body;
    QJsonObject diagnostics;
};

bool controlAccepted(const HttpResult& response)
{
    if (!response.ok) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(response.body, &error);
    if (error.error == QJsonParseError::NoError && document.isObject()) {
        const auto object = document.object();
        for (const char* key : {"success", "ok"}) {
            const auto value = object.value(QLatin1String(key));
            if (value.isBool() && !value.toBool()) return false;
        }
    }
    return true;
}

// One in-flight direct request, with a hard response and time limit. Cancellation
// disconnects callbacks before aborting, so an old reply cannot advance a session.
class HttpClient final : public QObject {
public:
    explicit HttpClient(QObject* parent = nullptr) : QObject(parent)
    {
        timeout_.setSingleShot(true);
    }
    ~HttpClient() override { cancel(); }
    std::function<void(QJsonObject)> onResult;

    void cancel()
    {
        timeout_.stop();
        disconnect(&timeout_, nullptr, this, nullptr);
        callback_ = {};
        body_.clear();
        postBuffer_.clear(); postReceived_ = postTrailerBytes_ = 0;
        postHeaders_ = postChunked_ = postTrailers_ = false;
        postStatus_ = 0; postLength_ = postChunkRemaining_ = -1;
        if (socket_) {
            disconnect(socket_, nullptr, this, nullptr);
            socket_->abort(); socket_->deleteLater(); socket_ = nullptr;
        }
    }
    void request(const QString& host, quint16 port, const QString& path,
                 const QJsonObject* post, std::function<void(HttpResult)> callback,
                 int timeoutMs = HttpTimeoutMs)
    {
        cancel();
        callback_ = std::move(callback);
        method_ = post ? "POST" : "GET";
        path_ = path;
        elapsed_.start();
        requestPost(host, port, post ? QJsonDocument(*post).toJson(QJsonDocument::Compact) : QByteArray{});
        timeout_.start(timeoutMs);
    }
private:
    void finish(bool ok, int status, int networkError, const char* reason)
    {
        auto callback = std::move(callback_);
        auto body = std::move(body_);
        const QString outcome = reason ? QString::fromLatin1(reason) :
            QString::fromLatin1(!ok ? status ? "http_status" : "transport" : "accepted");
        QJsonObject diagnostics{{"method", method_}, {"path", path_}, {"http_status", status},
            {"network_error", networkError}, {"elapsed_ms", double(elapsed_.elapsed())},
            {"reason", outcome}};
        {
            diagnostics.insert("request_http_version", "1.0");
            diagnostics.insert("request_format", "brainflow_reference");
            diagnostics.insert("request_body_bytes", double(postPayloadSize_));
        }
        // Retain bounded rejection text only for the TCP configuration request.
        // Never copy status/configuration responses that may contain secrets.
        if (!ok && method_ == "POST" && path_ == "/tcp" && !body.isEmpty()) {
            const auto document = QJsonDocument::fromJson(body);
            QString rejection;
            if (document.isObject()) {
                for (const char* key : {"error", "message", "reason"}) {
                    const auto value = document.object().value(QLatin1String(key));
                    if (value.isString()) { rejection = value.toString(); break; }
                }
            } else if (body.size() <= 512) rejection = QString::fromUtf8(body);
            rejection = rejection.left(512).simplified();
            const QRegularExpression sensitive("password|passwd|pwd|token|secret|credential|authorization|psk", QRegularExpression::CaseInsensitiveOption);
            if (sensitive.match(rejection).hasMatch()) rejection = "[redacted]";
            if (!rejection.isEmpty()) diagnostics.insert("response_error", rejection);
        }
        if (onResult) onResult(diagnostics);
        cancel(); // All transport callbacks are detached before advancing a session.
        if (callback) callback({ok, status, std::move(body), diagnostics});
    }
    void requestPost(const QString& host, quint16 port, const QByteArray& payload)
    {
        auto* current = new QTcpSocket(this);
        socket_ = current;
        current->setProxy(QNetworkProxy::NoProxy);
        current->setReadBufferSize(MaxPostWire + 1);
        postPayloadSize_ = payload.size();
        const QByteArray packet = method_.toLatin1() + " " + path_.toLatin1() + " HTTP/1.0\r\nHost: " + host.toLatin1() + ":" +
            QByteArray::number(port) + (method_ == "POST" ? "\r\nContent-Length: " + QByteArray::number(payload.size()) : QByteArray{}) + "\r\n\r\n" + payload;
        connect(current, &QTcpSocket::connected, this, [this, current, packet] {
            if (socket_ == current && current->write(packet) != packet.size())
                completePost(current, false, "transport");
        });
        connect(current, &QTcpSocket::readyRead, this, [this, current] { readPost(current, false); });
        connect(current, &QTcpSocket::disconnected, this, [this, current] { readPost(current, true); });
        connect(current, &QTcpSocket::errorOccurred, this, [this, current](QAbstractSocket::SocketError error) {
            if (socket_ != current) return;
            if (error == QAbstractSocket::RemoteHostClosedError) readPost(current, true);
            else completePost(current, false, "transport", int(error));
        });
        connect(&timeout_, &QTimer::timeout, this, [this, current] { completePost(current, false, "timeout"); });
        current->connectToHost(host, port);
    }
    void completePost(QTcpSocket* current, bool ok, const char* reason = nullptr, int networkError = 0)
    {
        if (socket_ == current) finish(ok, postStatus_, networkError, reason);
    }
    void readPost(QTcpSocket* current, bool eof)
    {
        if (socket_ != current) return;
        const auto bytes = current->read(MaxPostWire + 1 - postReceived_);
        postReceived_ += bytes.size();
        postBuffer_.append(bytes);
        if (postReceived_ > MaxPostWire) { completePost(current, false, "response_limit"); return; }
        if (!postHeaders_) {
            const auto end = postBuffer_.indexOf("\r\n\r\n");
            if (end < 0) {
                if (postBuffer_.size() > MaxPostHeaders) completePost(current, false, "response_limit");
                else if (eof) completePost(current, false, "truncated_response");
                return;
            }
            if (end > MaxPostHeaders) { completePost(current, false, "response_limit"); return; }
            const auto lines = postBuffer_.left(end).split('\n');
            const auto status = lines.front().trimmed().split(' ');
            bool valid = false;
            if (status.size() >= 2 && (status[0] == "HTTP/1.0" || status[0] == "HTTP/1.1") && status[1].size() == 3)
                postStatus_ = status[1].toInt(&valid);
            if (!valid || postStatus_ < 200 || postStatus_ > 599) {
                completePost(current, false, "invalid_response"); return;
            }
            bool haveLength = false, haveTransfer = false;
            for (int i = 1; i < lines.size(); ++i) {
                if (lines[i].startsWith(' ') || lines[i].startsWith('\t')) { completePost(current, false, "invalid_response"); return; }
                const auto line = lines[i].trimmed();
                const auto colon = line.indexOf(':');
                if (colon <= 0) { completePost(current, false, "invalid_response"); return; }
                const auto name = line.left(colon).trimmed().toLower();
                const auto value = line.mid(colon + 1).trimmed();
                if (name == "content-length") {
                    bool digits = !value.isEmpty();
                    for (const auto c : value) digits = digits && c >= '0' && c <= '9';
                    postLength_ = value.toLongLong(&valid);
                    if (haveLength || !digits || !valid) { completePost(current, false, "invalid_response"); return; }
                    if (postLength_ > MaxHttpBytes) { completePost(current, false, "response_limit"); return; }
                    haveLength = true;
                } else if (name == "transfer-encoding") {
                    if (haveTransfer || value.toLower() != "chunked") { completePost(current, false, "invalid_response"); return; }
                    postChunked_ = haveTransfer = true;
                }
            }
            if (haveLength && haveTransfer) { completePost(current, false, "invalid_response"); return; }
            if (postStatus_ == 204 || postStatus_ == 304) {
                if (postChunked_ || (haveLength && postLength_ != 0)) { completePost(current, false, "invalid_response"); return; }
                postLength_ = 0;
            }
            postBuffer_.remove(0, end + 4);
            postHeaders_ = true;
        }
        if (postChunked_) {
            while (true) {
                if (postTrailers_) {
                    const auto end = postBuffer_.indexOf("\r\n");
                    if (end < 0) {
                        if (postBuffer_.size() + postTrailerBytes_ > MaxPostHeaders) { completePost(current, false, "response_limit"); return; }
                        break;
                    }
                    const auto line = postBuffer_.left(end);
                    postBuffer_.remove(0, end + 2);
                    postTrailerBytes_ += end + 2;
                    if (postTrailerBytes_ > MaxPostHeaders) { completePost(current, false, "response_limit"); return; }
                    if (line.isEmpty()) {
                        if (!postBuffer_.isEmpty()) { completePost(current, false, "invalid_response"); return; }
                        completePost(current, postStatus_ < 300); return;
                    }
                    const auto colon = line.indexOf(':');
                    const auto name = line.left(colon).trimmed().toLower();
                    if (colon <= 0 || name == "content-length" || name == "transfer-encoding") {
                        completePost(current, false, "invalid_response"); return;
                    }
                    continue;
                }
                if (postChunkRemaining_ < 0) {
                    const auto end = postBuffer_.indexOf("\r\n");
                    if (end < 0) {
                        if (postBuffer_.size() > 256) { completePost(current, false, "invalid_response"); return; }
                        break;
                    }
                    if (end > 256) { completePost(current, false, "invalid_response"); return; }
                    const auto size = postBuffer_.left(end).split(';').front();
                    bool valid = !size.isEmpty();
                    for (const auto c : size)
                        valid = valid && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
                    bool number = false;
                    const auto n = size.toULongLong(&number, 16);
                    if (!valid || !number) { completePost(current, false, "invalid_response"); return; }
                    if (n > static_cast<quint64>(MaxHttpBytes - body_.size())) { completePost(current, false, "response_limit"); return; }
                    postBuffer_.remove(0, end + 2);
                    if (n == 0) { postTrailers_ = true; continue; }
                    postChunkRemaining_ = static_cast<qint64>(n);
                }
                if (postBuffer_.size() < postChunkRemaining_ + 2) break;
                if (postBuffer_.mid(postChunkRemaining_, 2) != "\r\n") { completePost(current, false, "invalid_response"); return; }
                body_.append(postBuffer_.left(postChunkRemaining_));
                postBuffer_.remove(0, postChunkRemaining_ + 2);
                postChunkRemaining_ = -1;
            }
        } else {
            if (body_.size() + postBuffer_.size() > MaxHttpBytes) { completePost(current, false, "response_limit"); return; }
            body_.append(postBuffer_); postBuffer_.clear();
            if (postLength_ >= 0) {
                if (body_.size() > postLength_) { completePost(current, false, "invalid_response"); return; }
                if (body_.size() == postLength_) { completePost(current, postStatus_ < 300); return; }
            } else if (eof) { completePost(current, postStatus_ < 300); return; }
        }
        if (eof) completePost(current, false, "truncated_response");
    }
    QTcpSocket* socket_ = nullptr;
    QByteArray postBuffer_;
    qint64 postReceived_ = 0, postTrailerBytes_ = 0, postPayloadSize_ = 0;
    qint64 postLength_ = -1, postChunkRemaining_ = -1;
    int postStatus_ = 0;
    bool postHeaders_ = false, postChunked_ = false, postTrailers_ = false;
    QTimer timeout_;
    QByteArray body_;
    QString method_, path_;
    QElapsedTimer elapsed_;
    std::function<void(HttpResult)> callback_;
};

QString reportedString(const QJsonObject& object, const char* key)
{
    const auto value = object.value(QLatin1String(key));
    if (!value.isString()) return {};
    const auto text = value.toString().trimmed();
    const auto lower = text.toLower();
    if (text.isEmpty() || text.size() > 128 || lower == "unknown" || lower == "none" ||
        lower == "null" || lower == "n/a") return {};
    return text;
}

struct BoardInfo {
    RtBciProbeResult probe;
    QString serial;
    bool busy = false;
};

bool knownBoardType(const QString& type)
{
    QString normalized;
    for (const auto character : type.toLower())
        if (character.isLetterOrNumber()) normalized.append(character);
    return normalized == "rtbci" || normalized == "rtbci8" || normalized == "rtbci16" ||
           normalized == "ads1299" || normalized == "ads12998" || normalized == "ads129916" ||
           normalized == "cyton" || normalized == "daisy" || normalized == "cytondaisy";
}

BoardInfo parseBoard(const QByteArray& bytes)
{
    BoardInfo info;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        info.probe.error = makeError("device.rt_bci_incompatible");
        return info;
    }
    const auto object = document.object();
    const auto connected = object.value("board_connected");
    if (!connected.isUndefined() && (!connected.isBool() || !connected.toBool())) {
        info.probe.error = makeError("device.rt_bci_incompatible");
        return info;
    }
    const auto type = reportedString(object, "board_type");
    const auto rawType = object.value("board_type");
    if (!rawType.isUndefined() && !rawType.isNull() && (type.isEmpty() || !knownBoardType(type))) {
        info.probe.error = makeError("device.rt_bci_incompatible");
        return info;
    }
    const auto channelValue = object.value("num_channels");
    int channels = 0;
    if (!channelValue.isUndefined() && !channelValue.isNull()) {
        const double n = channelValue.toDouble(-1);
        if (!channelValue.isDouble() || !std::isfinite(n) || std::floor(n) != n ||
            (n != 0 && n != 8 && n != 16)) {
            info.probe.error = makeError("device.rt_bci_incompatible");
            return info;
        }
        channels = static_cast<int>(n);
    }
    if (type.isEmpty() && !(connected.isBool() && connected.toBool() && channels)) {
        info.probe.error = makeError("device.rt_bci_incompatible");
        return info;
    }
    info.probe.compatible = true;
    info.probe.channels = channels;
    info.probe.model = type;
    info.serial = reportedString(object, "serial_number");
    info.probe.deviceId = info.serial;
    if (info.probe.deviceId.isEmpty()) info.probe.deviceId = reportedString(object, "device_id");
    for (const char* key : {"busy", "streaming", "is_streaming", "tcp_connected", "client_connected"})
        if (object.value(QLatin1String(key)).isBool() && object.value(QLatin1String(key)).toBool()) info.busy = true;
    const auto clientIp = reportedString(object, "client_ip");
    if (!reportedString(object, "owner").isEmpty() || (!clientIp.isEmpty() && clientIp != "0.0.0.0")) info.busy = true;
    return info;
}

bool rateAllowed(int rate) { return rate == 250 || rate == 500 || rate == 1000; }
bool gainAllowed(int gain)
{
    return gain == 0 || gain == 1 || gain == 2 || gain == 4 || gain == 6 || gain == 8 || gain == 12 || gain == 24;
}
QString rateCommand(int rate) { return rate == 250 ? "~6" : (rate == 500 ? "~5" : "~4"); }
double nominalScale(const RtBciWifiOptions& options)
{
    if (!options.gain) return 1.0;
    return static_cast<double>(static_cast<long double>(options.referenceVolts) * 1000000.0L /
                               (static_cast<long double>(options.gain) * 8388608.0L));
}

struct SharedState {
    mutable std::mutex mutex;
    AcquisitionViewSnapshot view;
    std::optional<Context> context;
    std::atomic_bool notificationPending{false};
};

class WifiWorker final : public QObject {
public:
    WifiWorker(std::shared_ptr<SharedState> shared, QPointer<AcquisitionWifiSource> owner)
        : shared_(std::move(shared)), owner_(owner), budget_(256 * 1024 * 1024) {}

    void setup()
    {
        http_ = new HttpClient(this);
        http_->onResult = [this](QJsonObject event) { log("http", event); };
        server_ = new QTcpServer(this);
        server_->setMaxPendingConnections(2);
        connect(server_, &QTcpServer::newConnection, this, [this] { acceptConnections(); });
        auto timer = [this](bool single, auto callback) {
            auto* result = new QTimer(this);
            result->setSingleShot(single);
            connect(result, &QTimer::timeout, this, callback);
            return result;
        };
        callbackTimer_ = timer(true, [this] { fail("device.rt_bci_callback"); });
        batchTimer_ = timer(true, [this] { if (!flush()) fail("buffer.budget_exhausted"); });
        silenceTimer_ = timer(false, [this] {
            log("receive_idle", {{"interval_ms", 3000}});
            if (hub_) {
                if (!flush()) { fail("buffer.budget_exhausted"); return; }
                hub_->reportUnknownGap(capture_->streams.front().key, "rt_bci.receive_idle");
            }
        });
        viewTimer_ = timer(false, [this] { refresh(); });
        viewTimer_->start(100);
    }

    void open(const RtBciWifiOptions& options)
    {
        if (stage_ != Stage::Disconnected && stage_ != Stage::Faulted) {
            update([](auto& view) { view.error = makeError("device.busy"); });
            return;
        }
        resetTransport();
        ++generation_;
        ++connectionRevision_;
        options_ = options;
        board_ = {};
        openLog();
        localAddress_.clear();
        boundPort_ = 0;
        QHostAddress host;
        if (!ipv4(options.host, host) || !options.httpPort) { failInitial("device.invalid_endpoint"); return; }
        if ((options.channels != 8 && options.channels != 16) || !rateAllowed(options.sampleRate) ||
            !gainAllowed(options.gain) || !std::isfinite(options.referenceVolts) || options.referenceVolts <= 0 ||
            !std::isfinite(nominalScale(options)) || nominalScale(options) <= 0) {
            failInitial("acquisition.invalid_configuration"); return;
        }
        host_ = host;
        stage_ = Stage::Probing;
        update([&](auto& view) {
            view = {};
            view.connectionRevision = connectionRevision_;
            view.disconnectionCompletion = disconnectionCompletion_;
            view.connection = DeviceConnectionState::Connecting;
            view.connectionPhase = "probing";
            view.sampleRate = options_.sampleRate;
            view.exactRate = {static_cast<std::uint32_t>(options_.sampleRate), 1};
            view.totalChannels = static_cast<std::size_t>(options_.channels);
            view.configurationVerified = false;
            view.deviceName = "RT-BCI";
        });
        const auto gen = generation_;
        http_->request(host_.toString(), options_.httpPort, "/board", nullptr, [this, gen](HttpResult response) {
            if (gen != generation_ || stage_ != Stage::Probing) return;
            if (!response.ok) { failHttp(response); return; }
            board_ = parseBoard(response.body);
            if (!board_.probe.compatible) { fail("device.rt_bci_incompatible"); return; }
            if (board_.busy) { fail("device.busy"); return; }
            // The RT-BCI Wi-Fi module can report 8 even when its acquisition
            // board supplies 16 channels. Like the reference GUI, use the
            // selected layout; the module's report is diagnostic, not ADC ACK.
            log("channel_selection", {{"requested_channels", options_.channels},
                {"reported_channels", board_.probe.channels}, {"channel_layout_source", "user_selected_unverified"},
                {"channel_report_mismatch", board_.probe.channels != 0 && board_.probe.channels != options_.channels}});
            if (!options_.expectedDeviceId.isEmpty() && options_.expectedDeviceId != board_.probe.deviceId) {
                fail("device.identity_mismatch"); return;
            }
            localAddress_ = routeAddress(host_, options_.localAddress);
            if (localAddress_.isEmpty()) { fail("device.rt_bci_callback"); return; }
            readTcpOwnership(gen);
        });
    }

    void configure(int rate)
    {
        if (stage_ != Stage::Ready || !rateAllowed(rate)) {
            update([&](auto& view) {
                view.error = makeError(stage_ == Stage::Ready ? "acquisition.invalid_configuration" : "device.busy");
                ++view.configCompletion;
            });
            return;
        }
        configuring_ = true;
        stage_ = Stage::Configuring;
        update([](auto& view) { view.configurationInProgress = true; view.canConfigure = false;
            view.configurationVerified = false; view.connectionPhase = "configuring"; view.error = {}; });
        commandRate(rate, false);
    }

    void start()
    {
        if (stage_ != Stage::Ready || !socket_) {
            update([](auto& view) { view.error = makeError("device.busy"); ++view.acquisitionCompletion; });
            return;
        }
        decoder_ = std::make_unique<rt_bci::Decoder>(static_cast<unsigned>(options_.channels));
        pending_.clear();
        writeSignalStats();
        log("stream_start_requested", {{"sample_rate", options_.sampleRate}});
        capture_.reset(); hub_.reset(); history_.reset();
        startPending_ = true;
        stopPending_ = false;
        stopRequested_ = false;
        startResponse_ = false;
        observed_ = false;
        pendingFraming_ = false;
        stage_ = Stage::Starting;
        update([](auto& view) {
            view.acquisition = DeviceAcquisitionState::Starting;
            view.canConfigure = false; view.error = {}; view.samples = view.missingSamples = view.unknownGaps = 0;
            view.traces.clear();
        });
        const auto gen = generation_;
        http_->request(host_.toString(), options_.httpPort, "/stream/start", nullptr, [this, gen](HttpResult response) {
            if (gen != generation_ || stage_ != Stage::Starting) return;
            if (!controlAccepted(response)) { failHttp(response); return; }
            startResponse_ = true;
            if (stopRequested_) { beginStop(); return; }
            if (!makeCapture()) { fail("buffer.budget_exhausted"); return; }
            silenceTimer_->start(3000);
            readSocket();
            streamingIfReady();
        });
    }

    void stop()
    {
        if (stage_ == Stage::Starting) {
            if (!stopPending_) stopPending_ = true;
            stopRequested_ = true;
            update([](auto& view) { view.acquisition = DeviceAcquisitionState::Stopping; });
            if (startResponse_) beginStop();
            return;
        }
        if (stage_ == Stage::Streaming) {
            stopPending_ = true;
            beginStop();
            return;
        }
        if (stage_ == Stage::Stopping) return;
        update([](auto& view) { ++view.acquisitionCompletion; });
    }

    void close(quint64 request = 0)
    {
        if (request) pendingCloseRequest_ = request;
        if (stage_ == Stage::Starting || stage_ == Stage::Streaming || stage_ == Stage::Stopping) {
            closeAfterStop_ = true;
            stop();
            return;
        }
        const bool config = configuring_;
        const bool faulted = stage_ == Stage::Faulted;
        disconnectionCompletion_ = std::max(disconnectionCompletion_, pendingCloseRequest_);
        ++generation_;
        resetTransport();
        stage_ = Stage::Disconnected;
        diagnostics_.close();
        configuring_ = false;
        update([&](auto& view) {
            view.connection = DeviceConnectionState::Disconnected; view.connectionPhase.clear();
            view.disconnectionCompletion = disconnectionCompletion_;
            if (faulted || view.acquisition == DeviceAcquisitionState::Faulted)
                view.acquisition = DeviceAcquisitionState::Faulted;
            else view.acquisition = DeviceAcquisitionState::Idle;
            view.canConfigure = false;
            if (config) { view.configurationInProgress = false; ++view.configCompletion; }
        });
    }

    void shutdown()
    {
        ++generation_;
        if (hub_) { flush(); terminate("rt_bci.disconnect_unknown"); }
        resetTransport();
    }

private:
    enum class Stage { Disconnected, Probing, StoppingOld, Waiting, Configuring, Ready, Starting, Streaming, Stopping, Faulted };

    template <class F> void update(F change)
    {
        { std::lock_guard<std::mutex> lock(shared_->mutex); change(shared_->view); shared_->view.diagnosticLogPath = diagnostics_.path(); }
        if (!owner_ || shared_->notificationPending.exchange(true)) return;
        const auto shared = shared_;
        const auto owner = owner_;
        QMetaObject::invokeMethod(owner.data(), [shared, owner] {
            shared->notificationPending.store(false);
            if (owner) emit owner->stateChanged();
        }, Qt::QueuedConnection);
    }
    void phase(const char* name) { update([&](auto& view) { view.connectionPhase = QLatin1String(name); }); }
    void failInitial(const char* code)
    {
        stage_ = Stage::Probing;
        update([&](auto& view) {
            view = {}; view.configurationVerified = false;
            view.connectionRevision = connectionRevision_;
            view.disconnectionCompletion = disconnectionCompletion_;
        });
        fail(code);
    }
    void readTcpOwnership(quint64 gen)
    {
        tcpOwnership_ = "not_reported";
        http_->request(host_.toString(), options_.httpPort, "/tcp", nullptr, [this, gen](HttpResult response) {
            if (gen != generation_ || stage_ != Stage::Probing) return;
            if (!response.ok) {
                if (response.status != 404 && response.status != 405 && response.status != 501) {
                    failHttp(response); return;
                }
                tcpOwnership_ = "endpoint_unavailable";
            } else {
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(response.body, &error);
                if (error.error != QJsonParseError::NoError || !document.isObject()) {
                    fail("device.rt_bci_incompatible"); return;
                }
                const auto object = document.object();
                const auto connected = object.value("connected");
                if (!connected.isUndefined() && !connected.isNull() && !connected.isBool()) {
                    fail("device.rt_bci_incompatible"); return;
                }
                if (connected.isBool()) {
                    if (connected.toBool()) { fail("device.busy"); return; }
                    tcpOwnership_ = "reported_disconnected_unverified";
                }
                const auto ip = reportedString(object, "ip");
                if (!connected.isBool() && !ip.isEmpty() && ip != "0.0.0.0") { fail("device.busy"); return; }
            }
            // Reference preparation establishes TCP before issuing board commands.
            // Do not inject /stream/stop before the callback is configured.
            listen();
        });
    }
    void listen()
    {
        phase("listening");
        QHostAddress bind;
        if (!ipv4(localAddress_, bind) || !server_->listen(bind, options_.listenPort)) {
            fail("device.rt_bci_callback"); return;
        }
        boundPort_ = server_->serverPort();
        stage_ = Stage::Waiting;
        callbackReceived_ = tcpResponse_ = false;
        phase("waiting");
        const QJsonObject body{{"ip", localAddress_}, {"port", int(server_->serverPort())}, {"output", "raw"},
                               {"delimiter", true}, {"latency", 10000}};
        const auto gen = generation_;
        http_->request(host_.toString(), options_.httpPort, "/tcp", &body, [this, gen](HttpResult response) {
            if (gen != generation_ || stage_ != Stage::Waiting) return;
            if (!controlAccepted(response)) { failHttp(response); return; }
            tcpResponse_ = true;
            // The module may wait for its TCP connect to finish before replying.
            // Give an accepted request its own callback deadline, and retain an
            // HTTP failure instead of racing it with a generic callback timeout.
            if (!callbackReceived_) callbackTimer_->start(CallbackTimeoutMs);
            maybeConfigure();
        });
    }
    void acceptConnections()
    {
        while (server_->hasPendingConnections()) {
            auto* connection = server_->nextPendingConnection();
            if (stage_ != Stage::Waiting || callbackReceived_ || connection->peerAddress().toIPv4Address() != host_.toIPv4Address()) {
                connection->abort(); connection->deleteLater(); continue;
            }
            callbackReceived_ = true;
            socket_ = connection;
            socket_->setReadBufferSize(256 * 1024);
            socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            socket_->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 256 * 1024);
            const auto gen = generation_;
            connect(socket_, &QTcpSocket::readyRead, this, [this, gen] { if (gen == generation_) readSocket(); });
            connect(socket_, &QTcpSocket::disconnected, this, [this, gen] {
                if (gen == generation_ && stage_ != Stage::Disconnected && stage_ != Stage::Faulted)
                    fail("device.disconnected");
            });
            server_->close();
            maybeConfigure();
        }
    }
    void maybeConfigure()
    {
        if (stage_ != Stage::Waiting || !tcpResponse_ || !callbackReceived_) return;
        callbackTimer_->stop();
        stage_ = Stage::Configuring;
        phase("configuring");
        initializeReference(0);
    }
    void initializeReference(int step)
    {
        if (step == 2) { commandRate(options_.sampleRate, true); return; }
        const QJsonObject command{{"command", step == 0 ? "~4" : "d"}};
        const auto gen = generation_;
        http_->request(host_.toString(), options_.httpPort, "/command", &command, [this, gen, step](HttpResult response) {
            if (gen != generation_ || stage_ != Stage::Configuring) return;
            if (!controlAccepted(response)) { failHttp(response); return; }
            initializeReference(step + 1);
        });
    }
    void commandRate(int rate, bool opening)
    {
        const QJsonObject command{{"command", rateCommand(rate)}};
        const auto gen = generation_;
        http_->request(host_.toString(), options_.httpPort, "/command", &command, [this, gen, rate, opening](HttpResult response) {
            if (gen != generation_ || stage_ != Stage::Configuring) return;
            if (!controlAccepted(response)) { failHttp(response); return; }
            options_.sampleRate = rate;
            configuring_ = false;
            stage_ = Stage::Ready;
            log(opening ? "connected" : "configured", {{"command", rateCommand(rate)}, {"sample_rate", rate}});
            update([&](auto& view) {
                view.connection = DeviceConnectionState::Connected; view.connectionPhase = "ready";
                view.acquisition = DeviceAcquisitionState::Idle; view.canConfigure = true;
                view.configurationInProgress = false; view.configurationVerified = false;
                view.sampleRate = rate; view.exactRate = {static_cast<std::uint32_t>(rate), 1};
                if (!opening) ++view.configCompletion;
            });
        });
    }
    void streamingIfReady()
    {
        if (stage_ != Stage::Starting || !startResponse_ || stopRequested_) return;
        if (!flush()) { fail("buffer.budget_exhausted"); return; }
        stage_ = Stage::Streaming;
        log("stream_started");
        const bool completed = std::exchange(startPending_, false);
        update([&](auto& view) { view.acquisition = DeviceAcquisitionState::Streaming;
            if (completed) ++view.acquisitionCompletion; });
    }
    bool makeCapture()
    {
        CaptureDescriptor capture;
        capture.session_id = {newIdentity()};
        capture.capture_id = {newIdentity()};
        capture.generation = generation_;
        capture.metadata = {
            {"adapter", "rt-bci.wifi"}, {"configuration_verified", "false"},
            {"transport_delimiter", "reference_fixed_33_byte_packets_no_stripping"},
            {"configuration_source", "user_requested_http_unverified"},
            {"requested_channels", std::to_string(options_.channels)},
            {"reported_channels", std::to_string(board_.probe.channels)},
            {"channel_layout_source", "user_selected_unverified"},
            {"channel_report_mismatch", board_.probe.channels != 0 && board_.probe.channels != options_.channels ? "true" : "false"},
            {"nominal_sample_rate_hz", std::to_string(options_.sampleRate)},
            {"gain", std::to_string(options_.gain)},
            {"gain_source", options_.gain ? "user_declared_unverified" : "unknown_raw_counts"},
            {"reference_volts", QString::number(options_.referenceVolts, 'g', 17).toStdString()},
            {"reference_source", options_.gain ? "nominal_input_unverified" : "not_used_for_raw_counts"},
            {"sample_index_meaning", "adapter_received_ordinal_not_device_counter"},
            {"counter_integrity", "wire_counter_logged_not_used_as_sample_clock_no_crc"},
            {"first_output_counter", decoder_->firstCounter() ? std::to_string(*decoder_->firstCounter()) : "unknown"},
            {"identity_strength", "weak"},
            {"identity_source", board_.probe.deviceId.isEmpty() ? "host_only" : "reported_unverified"},
            {"prior_tcp_ownership", tcpOwnership_.toStdString()},
            {"remote_tcp_release", "unverified_local_socket_close_only"},
            {"remote_stop_status", "unverified_http_is_not_hardware_confirmation"},
            {"capture_start_boundary", "unknown_device_cache_may_contain_old_samples"},
            {"capture_end_boundary", "unknown"},
            {"tail_groups", "determined_at_stop_as_gap"},
            {"clock_source", "estimated_session_relative"}
        };
        StreamDescriptor stream;
        stream.key = {capture.session_id, 1}; stream.capture_id = capture.capture_id; stream.generation = capture.generation;
        stream.name = "RT-BCI"; stream.signal_type = "biopotential";
        stream.rate = {static_cast<std::uint32_t>(options_.sampleRate), 1}; stream.dtype = DType::Int24;
        stream.clock = {1, static_cast<std::uint64_t>(options_.sampleRate), 1, ClockSource::Estimated, ClockEpoch::SessionRelative, false};
        stream.sample_index_source = SampleIndexSource::AdapterReceived; stream.max_samples_per_block = 64;
        for (int i = 0; i < options_.channels; ++i) {
            ChannelDescriptor channel;
            channel.channel_id = std::to_string(i + 1); channel.name = "CH" + std::to_string(i + 1);
            channel.signal_type = "biopotential"; channel.valid_adc_bits = 24;
            channel.unit = options_.gain ? Unit::Microvolt : Unit::Count;
            channel.scale = nominalScale(options_);
            stream.channels.push_back(std::move(channel));
        }
        capture.streams.push_back(std::move(stream));
        DeviceDescriptor device;
        device.device_id = {board_.probe.deviceId.isEmpty() ? "rt-bci.wifi@" + host_.toString().toStdString() : board_.probe.deviceId.toStdString()};
        device.boot_id = {newIdentity()}; device.generated_boot_id = true;
        // These compatibility fields have no authenticated provenance yet.
        device.weak_identity = true;
        device.model = board_.probe.model.isEmpty() ? "RT-BCI" : board_.probe.model.toStdString();
        device.serial_number = board_.serial.toStdString();
        device.adapter_version = "rt-bci.wifi/openbci-5.10/2";
        try {
            auto hub = std::make_shared<SignalHub>(capture, budget_);
            auto history = hub->addConsumer(ConsumerKind::History, {64 * 1024 * 1024, 1500});
            if (!history) return false;
            capture_ = std::move(capture); hub_ = std::move(hub); history_ = *history.value;
            { std::lock_guard<std::mutex> lock(shared_->mutex); shared_->context = Context{device, *capture_, hub_, history_}; }
            if (std::exchange(pendingFraming_, false))
                hub_->reportUnknownGap(capture_->streams.front().key, "rt_bci.framing_discard");
            return true;
        } catch (const std::exception&) { return false; }
    }
    void readSocket()
    {
        if (!socket_) return;
        if (stage_ != Stage::Starting && stage_ != Stage::Streaming && stage_ != Stage::Stopping) {
            return; // Preserve buffered bytes, as the reference does before start.
        }
        if (stage_ == Stage::Stopping || (stage_ == Stage::Starting && !startResponse_)) return;
        // The reference keeps receiving after timeouts and protocol warnings.
        // Host scheduling delay never proves a device-counter discontinuity.
        qint64 remaining = MaxSocketRead;
        while (socket_ && remaining > 0 && socket_->bytesAvailable() > 0 && decoder_) {
            const auto bytes = socket_->read(std::min<qint64>(remaining, 16384));
            if (bytes.isEmpty()) break;
            remaining -= bytes.size();
            auto batch = decoder_->feed(reinterpret_cast<const std::uint8_t*>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
            if (!consume(batch)) return;
            if (!batch.samples.empty()) {
                if (stage_ != Stage::Stopping)
                    silenceTimer_->start(3000);
            }
        }
        if (socket_ && socket_->bytesAvailable() > 0) {
            const auto gen = generation_;
            QTimer::singleShot(0, this, [this, gen] { if (gen == generation_) readSocket(); });
        }
    }
    bool consume(const rt_bci::Batch& batch)
    {
        for (const auto& note : batch.diagnostics) {
            const auto bytes = QByteArray(reinterpret_cast<const char*>(note.bytes.data()), static_cast<qsizetype>(note.bytes.size()));
            log("wire_warning", {{"reason", QString::fromStdString(note.reason)},
                {"byte_offset", QString::number(note.byte_offset)}, {"raw_hex", QString::fromLatin1(bytes.toHex())}});
        }
        // The decoder may return a valid prefix and a terminal error together.
        for (const auto& sample : batch.samples) {
            if (!hub_ && !makeCapture()) { fail("buffer.budget_exhausted"); return false; }
            if (sample.gap_before) {
                if (!flush()) { fail("buffer.budget_exhausted"); return false; }
                hub_->reportUnknownGap(capture_->streams.front().key, "rt_bci.wire_discontinuity");
            }
            if (!pending_.empty() && sample.index != pending_.back().index + 1 && !flush()) {
                fail("buffer.budget_exhausted"); return false;
            }
            if (pending_.empty()) {
                pendingReceivedNs_ = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                batchTimer_->start(20);
            }
            pending_.push_back(sample);
            observeSignal(sample);
            const auto limit = std::min(64, std::max(1, options_.sampleRate / 50));
            if (static_cast<int>(pending_.size()) >= limit && !flush()) { fail("buffer.budget_exhausted"); return false; }
            if (!observed_) log("first_data", {{"counter", int(sample.counter)}});
            observed_ = true;
        }
        if (batch.discarded_bytes && batch.samples.empty()) {
            if (hub_) {
                if (!flush()) { fail("buffer.budget_exhausted"); return false; }
                hub_->reportUnknownGap(capture_->streams.front().key, "rt_bci.framing_discard");
            } else pendingFraming_ = true;
        }
        if (batch.error) {
            if (!flush()) { fail("buffer.budget_exhausted"); return false; }
            QJsonObject details{{"decoder_code", QString::fromStdString(batch.error->code)},
                {"discarded_bytes", QString::number(batch.discarded_bytes)}};
            for (const auto& parameter : batch.error->params)
                details.insert(QString::fromStdString(parameter.first), QString::fromStdString(parameter.second));
            fail("device.rt_bci_invalid_data", details); return false;
        }
        streamingIfReady();
        return true;
    }
    bool flush()
    {
        if (pending_.empty()) return true;
        batchTimer_->stop();
        if (!hub_ || !capture_) return false;
        std::vector<std::int32_t> values;
        values.reserve(static_cast<std::size_t>(options_.channels) * pending_.size());
        for (int ch = 0; ch < options_.channels; ++ch)
            for (const auto& sample : pending_) values.push_back(sample.values[static_cast<std::size_t>(ch)]);
        auto storage = SampleStorage::fromInt32(DType::Int24, values, budget_);
        if (!storage) return false;
        const auto& descriptor = capture_->streams.front();
        SampleBlock block;
        block.stream = descriptor.key; block.capture_id = capture_->capture_id; block.generation = capture_->generation;
        block.descriptor_revision = descriptor.revision; block.channel_count = static_cast<std::uint32_t>(options_.channels);
        block.sample_count = static_cast<std::uint32_t>(pending_.size()); block.first_sample_index = pending_.front().index;
        block.first_timestamp_ticks = block.first_sample_index; block.clock_id = descriptor.clock.clock_id;
        block.timestamp_estimated = true; block.host_received_monotonic_ns = pendingReceivedNs_;
        block.host_clock_id = "std::chrono::steady_clock"; block.storage = std::move(storage);
        const auto result = hub_->publish(block);
        pending_.clear();
        return result.status == PublishStatus::Accepted;
    }
    void beginStop()
    {
        if (stage_ != Stage::Starting && stage_ != Stage::Streaming) return;
        stage_ = Stage::Stopping;
        silenceTimer_->stop();
        stopAcknowledged_ = false;
        update([](auto& view) { view.acquisition = DeviceAcquisitionState::Stopping; view.canConfigure = false; });
        const auto gen = generation_;
        http_->request(host_.toString(), options_.httpPort, "/stream/stop", nullptr, [this, gen](HttpResult response) {
            if (gen != generation_ || stage_ != Stage::Stopping) return;
            if (!controlAccepted(response)) { failHttp(response); return; }
            stopAcknowledged_ = true;
            finishStop();
        }, StopLimitMs);
    }
    void finishStop()
    {
        if (stage_ != Stage::Stopping || !stopAcknowledged_) return;
        if (decoder_) {
            const auto batch = decoder_->finish();
            if (!consume(batch)) return;
        }
        if (!flush()) { fail("buffer.budget_exhausted"); return; }
        writeSignalStats();
        log("stream_stopped");
        const auto discarded = decoder_ ? decoder_->discardedGroups() : 0;
        const std::string reason = discarded ? "rt_bci.unconfirmed_tail_groups=" + std::to_string(discarded) : "rt_bci.end_boundary_unknown";
        terminate(reason);
        decoder_.reset();
        const bool startDone = std::exchange(startPending_, false);
        const bool stopDone = std::exchange(stopPending_, false);
        stage_ = Stage::Ready;
        update([&](auto& view) {
            view.acquisition = DeviceAcquisitionState::Idle; view.canConfigure = true;
            if (startDone) ++view.acquisitionCompletion;
            if (stopDone) ++view.acquisitionCompletion;
        });
        if (std::exchange(closeAfterStop_, false)) close();
    }
    void refresh()
    {
        if (!hub_ || !capture_ || !history_) return;
        const auto stats = hub_->stats();
        auto traces = buildAcquisitionTraces(*capture_, history_);
        update([&](auto& view) {
            view.samples = stats.accepted_samples; view.missingSamples = stats.known_missing_samples;
            view.unknownGaps = stats.unknown_gaps; view.traces = std::move(traces);
        });
    }
    void terminate(const std::string& reason)
    {
        if (!hub_ || !capture_) return;
        hub_->reportUnknownGap(capture_->streams.front().key, reason);
        refresh();
        hub_->close();
        { std::lock_guard<std::mutex> lock(shared_->mutex); shared_->context.reset(); }
        hub_.reset(); history_.reset(); capture_.reset();
    }
    void resetTransport()
    {
        writeSignalStats();
        if (http_) http_->cancel();
        for (auto* timer : {callbackTimer_, batchTimer_, silenceTimer_})
            if (timer) timer->stop();
        if (server_) server_->close();
        if (socket_) {
            disconnect(socket_, nullptr, this, nullptr);
            socket_->abort(); socket_->deleteLater(); socket_ = nullptr;
        }
        decoder_.reset(); pending_.clear(); closeAfterStop_ = false;
    }
    void failHttp(const HttpResult& response)
    {
        auto diagnostics = response.diagnostics;
        if (diagnostics.value("method") == "POST" && diagnostics.value("path") == "/tcp")
            diagnostics.insert("tcp_request", QJsonObject{{"ip", localAddress_}, {"port", int(boundPort_)},
                {"output", "raw"}, {"delimiter", true}, {"latency", 10000}});
        // A 2xx body may still explicitly reject a command. Never mark that as
        // success, and never resend configuration or start automatically.
        if (response.ok) diagnostics.insert("reason", "rejected");
        fail(response.diagnostics.value("method") == "POST" && response.diagnostics.value("path") == "/tcp" &&
             response.status == 504 ? "device.rt_bci_callback" : "device.rt_bci_http", diagnostics);
    }
    void fail(const char* code, QJsonObject diagnostics = {})
    {
        if (stage_ == Stage::Faulted || stage_ == Stage::Disconnected) return;
        const bool wasAcquiring = stage_ == Stage::Starting || stage_ == Stage::Streaming || stage_ == Stage::Stopping;
        { std::lock_guard<std::mutex> lock(shared_->mutex); diagnostics.insert("phase", shared_->view.connectionPhase); }
        diagnostics.insert("host", host_.toString());
        diagnostics.insert("http_port", options_.httpPort);
        diagnostics.insert("diagnostic_log", diagnostics_.path());
        diagnostics.insert("local_address", localAddress_);
        diagnostics.insert("listen_port", int(boundPort_ ? boundPort_ : options_.listenPort));
        diagnostics.insert("requested_channels", options_.channels);
        diagnostics.insert("reported_channels", board_.probe.channels);
        diagnostics.insert("requested_sample_rate", options_.sampleRate);
        diagnostics.insert("start_http_accepted", startResponse_);
        diagnostics.insert("valid_samples_observed", observed_);
        if (hub_) diagnostics.insert("accepted_samples", QString::number(hub_->stats().accepted_samples + pending_.size()));
        auto failureEvent = diagnostics; failureEvent.insert("code", QLatin1String(code));
        log("failure", failureEvent);
        if (hub_) { flush(); terminate("rt_bci.interrupted_unknown"); }
        const bool configDone = std::exchange(configuring_, false);
        const bool startDone = std::exchange(startPending_, false);
        const bool stopDone = std::exchange(stopPending_, false);
        ++generation_;
        resetTransport();
        stage_ = Stage::Faulted;
        update([&](auto& view) {
            view.connection = DeviceConnectionState::Faulted; view.connectionPhase.clear(); view.canConfigure = false;
            if (wasAcquiring) view.acquisition = DeviceAcquisitionState::Faulted;
            if (configDone) { view.configurationInProgress = false; ++view.configCompletion; }
            if (startDone) ++view.acquisitionCompletion;
            if (stopDone) ++view.acquisitionCompletion;
            view.error = makeError(QLatin1String(code), diagnostics);
        });
    }

    void observeSignal(const rt_bci::Sample& sample)
    {
        if (capture_) diagnostics_.observeInt24(sample.values.data(), capture_->streams.front());
    }
    void writeSignalStats() { diagnostics_.flushStatistics(); }
    void openLog()
    {
        diagnostics_.open("rt-bci.wifi", false, {{"host", options_.host}, {"channels", options_.channels},
            {"sample_rate", options_.sampleRate}, {"receive_profile", "brainflow_5.10.0_cyton_wifi"}});
    }
    void log(const char* kind, QJsonObject event = {}) { diagnostics_.write(QLatin1String(kind), event); }
    AcquisitionDiagnosticLog diagnostics_;
    std::shared_ptr<SharedState> shared_;
    QPointer<AcquisitionWifiSource> owner_;
    MemoryBudget budget_;
    HttpClient* http_ = nullptr;
    QTcpServer* server_ = nullptr;
    QTcpSocket* socket_ = nullptr;
    QTimer* callbackTimer_ = nullptr;
    QTimer *batchTimer_ = nullptr, *silenceTimer_ = nullptr, *viewTimer_ = nullptr;
    Stage stage_ = Stage::Disconnected;
    quint64 generation_ = 0;
    quint64 connectionRevision_ = 0, disconnectionCompletion_ = 0, pendingCloseRequest_ = 0;
    RtBciWifiOptions options_;
    QHostAddress host_;
    QString localAddress_;
    QString tcpOwnership_;
    BoardInfo board_;
    bool callbackReceived_ = false, tcpResponse_ = false, configuring_ = false;
    quint16 boundPort_ = 0;
    bool startPending_ = false, stopPending_ = false, stopRequested_ = false, closeAfterStop_ = false;
    bool startResponse_ = false, observed_ = false, stopAcknowledged_ = false;
    bool pendingFraming_ = false;
    std::unique_ptr<rt_bci::Decoder> decoder_;
    std::vector<rt_bci::Sample> pending_;
    std::uint64_t pendingReceivedNs_ = 0;
    std::optional<CaptureDescriptor> capture_;
    std::shared_ptr<SignalHub> hub_;
    std::shared_ptr<SignalConsumer> history_;
};
} // namespace

struct RtBciWifiProbe::Impl {
    explicit Impl(RtBciWifiProbe* owner) : q(owner), http(new HttpClient(owner)) {}
    RtBciWifiProbe* q;
    HttpClient* http;
    quint64 generation = 0;
};

RtBciWifiProbe::RtBciWifiProbe(QObject* parent) : QObject(parent), d(std::make_unique<Impl>(this)) {}
RtBciWifiProbe::~RtBciWifiProbe() { d->http->cancel(); }
quint64 RtBciWifiProbe::probe(const QString& host, quint16 port)
{
    cancel();
    const auto gen = d->generation;
    QHostAddress address;
    if (!ipv4(host, address) || !port) {
        QTimer::singleShot(0, this, [this, gen] {
            if (gen != d->generation) return;
            RtBciProbeResult result; result.error = makeError("device.invalid_endpoint");
            emit finished(gen, result);
        });
        return gen;
    }
    d->http->request(address.toString(), port, "/board", nullptr, [this, gen, address](HttpResult response) {
        if (gen != d->generation) return;
        RtBciProbeResult result;
        if (!response.ok) result.error = makeError("device.rt_bci_http", response.diagnostics);
        else {
            const auto board = parseBoard(response.body);
            result = board.probe;
            if (board.busy && result.compatible) result.error = makeError("device.busy");
            result.localAddress = routeAddress(address);
        }
        emit finished(gen, result);
    });
    return gen;
}
void RtBciWifiProbe::cancel() { ++d->generation; d->http->cancel(); }

struct AcquisitionWifiSource::Impl {
    explicit Impl(AcquisitionWifiSource* owner) : shared(std::make_shared<SharedState>())
    {
        worker = new WifiWorker(shared, owner);
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
    WifiWorker* worker = nullptr;
};

AcquisitionWifiSource::AcquisitionWifiSource(QObject* parent) : AcquisitionSource(parent), d(std::make_unique<Impl>(this)) {}
AcquisitionWifiSource::~AcquisitionWifiSource() = default;
void AcquisitionWifiSource::open(const RtBciWifiOptions& options)
{ QMetaObject::invokeMethod(d->worker, [worker = d->worker, options] { worker->open(options); }, Qt::QueuedConnection); }
void AcquisitionWifiSource::configure(int rate)
{ QMetaObject::invokeMethod(d->worker, [worker = d->worker, rate] { worker->configure(rate); }, Qt::QueuedConnection); }
void AcquisitionWifiSource::startAcquisition()
{ QMetaObject::invokeMethod(d->worker, [worker = d->worker] { worker->start(); }, Qt::QueuedConnection); }
void AcquisitionWifiSource::stopAcquisition()
{ QMetaObject::invokeMethod(d->worker, [worker = d->worker] { worker->stop(); }, Qt::QueuedConnection); }
void AcquisitionWifiSource::close()
{
    const auto request = beginClose();
    QMetaObject::invokeMethod(d->worker, [worker = d->worker, request] { worker->close(request); }, Qt::QueuedConnection);
}
AcquisitionViewSnapshot AcquisitionWifiSource::snapshot() const
{ std::lock_guard<std::mutex> lock(d->shared->mutex); return d->shared->view; }
std::optional<AcquisitionSource::CaptureContext> AcquisitionWifiSource::captureContext() const
{ std::lock_guard<std::mutex> lock(d->shared->mutex); return d->shared->context; }

} // namespace bf
