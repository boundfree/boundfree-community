#include "controller/acquisition_wifi_source.h"
#include "controller/acquisition_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace bf;
using namespace bf::acquisition;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x); } while (false)

namespace {
bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 4000)
{
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    QCoreApplication::processEvents();
    return predicate();
}

// Used only while forcing a GUI-thread notification race. The source worker
// can finish its queued close while its GUI notification remains undelivered.
bool waitWithoutGuiEvents(const std::function<bool()>& predicate, int timeoutMs = 2000)
{
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs) QThread::msleep(1);
    return predicate();
}

// Independent wire encoder: expected integers come directly from this formula,
// never from the decoder being tested.
std::int32_t valueFor(std::uint64_t group, int channel)
{
    const auto value = static_cast<std::int32_t>(100000 + group * 17 + channel * 1000);
    return channel % 2 ? -value : value;
}
QByteArray halfFrame(std::uint8_t counter, std::uint64_t group, int offset)
{
    QByteArray frame(33, '\0');
    frame[0] = char(0xa0); frame[1] = char(counter); frame[32] = char(0xc0);
    for (int ch = 0; ch < 8; ++ch) {
        const auto bits = static_cast<std::uint32_t>(valueFor(group, offset + ch)) & 0xffffffu;
        frame[2 + 3 * ch] = char(bits >> 16); frame[3 + 3 * ch] = char(bits >> 8); frame[4 + 3 * ch] = char(bits);
    }
    return frame;
}

class VirtualDevice final : public QObject {
public:
    explicit VirtualDevice(int channelCount = 8) : channels(channelCount)
    {
        board = QJsonObject{{"board_connected", true}, {"board_type", "Cyton"}, {"num_channels", channels}};
        CHECK(http.listen(QHostAddress::LocalHost, 0));
        connect(&http, &QTcpServer::newConnection, this, [this] {
            while (http.hasPendingConnections()) {
                auto* socket = http.nextPendingConnection();
                buffers.insert({socket, {}});
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    buffers.erase(socket); socket->deleteLater();
                });
            }
        });
        samples.setInterval(20);
        connect(&samples, &QTimer::timeout, this, [this] { sendGroups(sampleRate / 50); });
    }
    ~VirtualDevice() override
    {
        samples.stop();
        for (auto& entry : buffers) disconnect(entry.first, nullptr, this, nullptr);
        if (data) disconnect(data, nullptr, this, nullptr);
    }
    RtBciWifiOptions options() const
    {
        RtBciWifiOptions result;
        result.host = "127.0.0.1"; result.httpPort = http.serverPort(); result.listenPort = 0;
        result.channels = 0; return result;
    }
    void sendGroups(int count)
    {
        if (!data || data->state() != QAbstractSocket::ConnectedState) return;
        QByteArray bytes;
        for (int i = 0; i < count; ++i) {
            const auto counter = static_cast<std::uint8_t>((254 + nextGroup) & 255);
            bytes += halfFrame(counter, nextGroup, 0);
            if (emitDelimiters) bytes += "\r\n";
            if (channels == 16) { bytes += halfFrame(counter, nextGroup, 8); if (emitDelimiters) bytes += "\r\n"; }
            ++nextGroup;
        }
        data->write(bytes);
    }
    void sendFault()
    {
        samples.stop();
        CHECK(data);
        const auto c = static_cast<std::uint8_t>((254 + nextGroup) & 255);
        QByteArray bytes = halfFrame(c, nextGroup, 0);
        ++nextGroup;
        const auto next = static_cast<std::uint8_t>((254 + nextGroup) & 255);
        bytes += halfFrame(next, nextGroup, 0);
        bytes += halfFrame(next, nextGroup, 0); // duplicate after a valid prefix
        data->write(bytes);
    }
    void disconnectData() { if (data) data->abort(); }
    void skipGroup() { ++nextGroup; }
    bool hasPendingTcpReply() const { return !pendingTcpReply.isNull(); }
    void releaseTcpReply()
    {
        CHECK(pendingTcpReply);
        auto* socket = pendingTcpReply.data();
        pendingTcpReply.clear();
        respond(socket, failTcp ? 500 : tcpPostStatus, tcpPostBody, tcpDelayMs);
    }
    void recordDataEvent(const char* event)
    {
        if (!traceDataEvents || !data) return;
        dataEvents.push_back(QStringLiteral("%1ms %2 state=%3 error=%4 errorString=%5 local=%6:%7 peer=%8:%9 bytesAvailable=%10 bytesToWrite=%11")
            .arg(dataClock.elapsed()).arg(QLatin1String(event)).arg(static_cast<int>(data->state()))
            .arg(static_cast<int>(data->error())).arg(data->errorString())
            .arg(data->localAddress().toString()).arg(data->localPort())
            .arg(data->peerAddress().toString()).arg(data->peerPort())
            .arg(data->bytesAvailable()).arg(data->bytesToWrite()));
    }

    int channels;
    int sampleRate = 250;
    QJsonObject board;
    QJsonObject tcpStatus{{"connected", false}, {"ip", "0.0.0.0"}};
    QByteArray boardBytes;
    bool noCallback = false;
    bool emitDelimiters = false;
    QByteArray tcpPostBody = "{}";
    QByteArray commandWireReply;
    bool strictReference = true;
    int replyFragment = 0;
    bool failTcp = false;
    bool deferTcpReply = false;
    bool traceDataEvents = false;
    QElapsedTimer dataClock;
    QStringList dataEvents;
    bool noStartData = false;
    bool failStop = false;
    bool failCommand = false;
    bool rejectCommand = false;
    int boardDelayMs = 0;
    int tcpDelayMs = 0;
    int startDelayMs = 0;
    int stopDelayMs = 0;
    int commandDelayMs = 0;
    int boardStatus = 200;
    int tcpStatusCode = 200;
    int tcpPostStatus = 200, commandStatus = 200, startStatus = 200;
    QStringList paths;
    std::vector<QByteArray> postWire;
    QJsonObject tcpRequest, commandRequest;
    QTimer samples;
    QPointer<QTcpSocket> data;
    std::uint64_t nextGroup = 0;
private:
    void readRequest(QTcpSocket* socket)
    {
        auto it = buffers.find(socket);
        if (it == buffers.end()) return;
        auto& bytes = it->second;
        bytes += socket->readAll();
        const int headerEnd = bytes.indexOf("\r\n\r\n");
        if (headerEnd < 0) return;
        const auto lines = bytes.left(headerEnd).split('\n');
        if (lines.isEmpty()) return;
        const auto first = lines.first().trimmed().split(' ');
        if (first.size() < 2) return;
        int length = 0;
        for (const auto& line : lines) {
            const auto trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:")) length = trimmed.mid(15).trimmed().toInt();
        }
        if (bytes.size() < headerEnd + 4 + length) return;
        const auto path = QString::fromLatin1(first[1]);
        const auto method = first[0];
        if (method == "POST") postWire.push_back(bytes.left(headerEnd + 4 + length));
        const auto body = QJsonDocument::fromJson(bytes.mid(headerEnd + 4, length)).object();
        disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
        paths.push_back(path);
        if (strictReference) {
            // Model the exact envelope accepted in the supplied hardware log.
            if (first.size() != 3 || first[2] != "HTTP/1.0" ||
                (method == "POST" && !bytes.left(headerEnd).contains("\r\nContent-Length: ")) ||
                !bytes.left(headerEnd).contains("\r\nHost: ") ||
                bytes.left(headerEnd).toLower().contains("content-type:")) {
                respond(socket, 400, "{\"error\":\"Invalid JSON\"}"); return;
            }
        }
        if (path == "/board") {
            const QByteArray response = boardBytes.isNull() ? QJsonDocument(board).toJson(QJsonDocument::Compact) : boardBytes;
            respond(socket, boardStatus, response, boardDelayMs);
        } else if (path == "/tcp" && method == "GET") {
            respond(socket, tcpStatusCode, QJsonDocument(tcpStatus).toJson(QJsonDocument::Compact));
        } else if (path == "/tcp" && method == "POST") {
            tcpRequest = body;
            if (!noCallback) {
                data = new QTcpSocket(this);
                if (traceDataEvents) {
                    connect(data, &QTcpSocket::stateChanged, this, [this](QAbstractSocket::SocketState) { recordDataEvent("stateChanged"); });
                    connect(data, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { recordDataEvent("errorOccurred"); });
                    connect(data, &QTcpSocket::connected, this, [this] { recordDataEvent("connected"); });
                    connect(data, &QTcpSocket::disconnected, this, [this] { recordDataEvent("disconnected"); });
                    recordDataEvent("before connectToHost");
                }
                data->connectToHost(body.value("ip").toString(), static_cast<quint16>(body.value("port").toInt()));
                recordDataEvent("after connectToHost");
            }
            if (deferTcpReply) pendingTcpReply = socket;
            else respond(socket, failTcp ? 500 : tcpPostStatus, tcpPostBody, tcpDelayMs);
        } else if (path == "/command" && method == "POST") {
            commandRequest = body;
            if (!commandWireReply.isNull()) {
                respondRaw(socket, commandWireReply, replyFragment); return;
            }
            if (!failCommand && !rejectCommand) {
                const auto command = body.value("command").toString();
                if (command == "~6") sampleRate = 250;
                else if (command == "~5") sampleRate = 500;
                else if (command == "~4") sampleRate = 1000;
            }
            respond(socket, failCommand ? 500 : commandStatus, rejectCommand ? "{\"success\":false}" : "{}", commandDelayMs);
        } else if (path == "/stream/start") {
            nextGroup = 0;
            respond(socket, startStatus, "{}", startDelayMs);
            if (!noStartData && startStatus == 200) samples.start();
        } else if (path == "/stream/stop") {
            samples.stop();
            respond(socket, failStop ? 500 : 200, "{}", stopDelayMs);
        } else respond(socket, 404, "{}");
    }
    static void respond(QTcpSocket* socket, int status, const QByteArray& body, int delay = 0)
    {
        QPointer<QTcpSocket> guarded(socket);
        QTimer::singleShot(delay, socket, [guarded, status, body] {
            if (!guarded) return;
            const auto headers = QByteArray("HTTP/1.1 ") + QByteArray::number(status) + " test\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
            guarded->write(headers + body);
            guarded->disconnectFromHost();
        });
    }
    static void respondRaw(QTcpSocket* socket, const QByteArray& wire, int fragment)
    {
        if (!fragment) { socket->write(wire); socket->disconnectFromHost(); return; }
        auto* timer = new QTimer(socket);
        auto position = std::make_shared<int>(0);
        QObject::connect(timer, &QTimer::timeout, socket, [socket, wire, fragment, timer, position] {
            socket->write(wire.mid(*position, fragment));
            *position += fragment;
            if (*position >= wire.size()) { timer->stop(); socket->disconnectFromHost(); }
        });
        timer->start(1);
    }
    QTcpServer http;
    QPointer<QTcpSocket> pendingTcpReply;
    std::map<QTcpSocket*, QByteArray> buffers;
};

void expectReady(AcquisitionWifiSource& source, VirtualDevice& device, RtBciWifiOptions options)
{
    const auto revision = source.snapshot().connectionRevision;
    source.open(options);
    CHECK(waitFor([&] { auto s = source.snapshot(); return s.connectionRevision > revision &&
        (s.connection == DeviceConnectionState::Connected || s.connection == DeviceConnectionState::Faulted); }));
    auto state = source.snapshot();
    CHECK(state.connection == DeviceConnectionState::Connected);
    CHECK(state.acquisition == DeviceAcquisitionState::Idle && state.connectionPhase == "ready");
    CHECK(state.canConfigure && !state.configurationVerified);
    CHECK(!device.paths.contains("/stream/start"));
    CHECK(device.tcpRequest.value("ip").toString() == "127.0.0.1");
    CHECK(device.tcpRequest.value("port").toInt() > 0);
    CHECK(device.tcpRequest.value("output").toString() == "raw");
    CHECK(device.tcpRequest.value("delimiter").isBool() && device.tcpRequest.value("delimiter").toBool());
    CHECK(device.tcpRequest.value("latency").toInt() == 10000);
    CHECK(device.commandRequest.value("command").toString() ==
          (options.sampleRate == 250 ? "~6" : options.sampleRate == 500 ? "~5" : "~4"));
    CHECK(device.postWire.size() >= 4);
    CHECK(device.postWire[1].endsWith("{\"command\":\"~4\"}"));
    CHECK(device.postWire[2].endsWith("{\"command\":\"d\"}"));
    CHECK(state.sampleRate == options.sampleRate && state.exactRate.numerator == static_cast<unsigned>(options.sampleRate));
}

void testProbe()
{
    VirtualDevice device;
    RtBciWifiProbe probe;
    int count = 0;
    quint64 received = 0;
    RtBciProbeResult result;
    QObject::connect(&probe, &RtBciWifiProbe::finished, &probe, [&](quint64 gen, const RtBciProbeResult& value) {
        ++count; received = gen; result = value;
    });
    const auto gen = probe.probe("127.0.0.1", device.options().httpPort);
    CHECK(waitFor([&] { return count == 1; }));
    CHECK(received == gen && result.compatible && result.channels == 8 && result.localAddress == "127.0.0.1");
    CHECK(device.paths == QStringList{"/board"});
    device.board = {{"board_connected", true}};
    probe.probe("127.0.0.1", device.options().httpPort);
    CHECK(waitFor([&] { return count == 2; }));
    CHECK(result.compatible && result.channels == 0);
    device.board = {{"board_connected", true}, {"num_channels", 8}, {"board_type", "Ganglion"}};
    probe.probe("127.0.0.1", device.options().httpPort);
    CHECK(waitFor([&] { return count == 3; }));
    CHECK(!result.compatible);
    device.board = {{"board_type", "ADS1299"}};
    probe.probe("127.0.0.1", device.options().httpPort);
    CHECK(waitFor([&] { return count == 4; }));
    CHECK(result.compatible && result.channels == 0);
    device.board = {{"board_connected", true}, {"num_channels", 8}};
    device.boardDelayMs = 200;
    probe.probe("127.0.0.1", device.options().httpPort);
    CHECK(waitFor([&] { return device.paths.size() >= 5; }));
    probe.cancel(); device.boardDelayMs = 0;
    const auto replacement = probe.probe("127.0.0.1", device.options().httpPort);
    CHECK(waitFor([&] { return count == 5; }));
    CHECK(received == replacement && result.compatible);
    QElapsedTimer delay; delay.start();
    while (delay.elapsed() < 250) { QCoreApplication::processEvents(); QThread::msleep(1); }
    CHECK(count == 5);
    device.board.insert("busy", true);
    probe.probe("127.0.0.1", device.options().httpPort);
    CHECK(waitFor([&] { return count == 6; }));
    CHECK(result.compatible && result.error.code == "device.busy");
}

void testReentrantOverlimit()
{
    VirtualDevice device;
    device.boardBytes = QByteArray(65537, 'x');
    RtBciWifiProbe probe;
    int count = 0;
    bool secondOk = false;
    bool firstWasLimited = false;
    QObject::connect(&probe, &RtBciWifiProbe::finished, &probe, [&](quint64, const RtBciProbeResult& result) {
        ++count;
        if (count == 1) {
            firstWasLimited = !result.compatible && result.error.code == "device.rt_bci_http";
            device.boardBytes = QByteArray();
            probe.probe("127.0.0.1", device.options().httpPort); // re-enters the HTTP callback
        } else secondOk = result.compatible;
    });
    probe.probe("127.0.0.1", device.options().httpPort);
    CHECK(waitFor([&] { return count == 2; }));
    CHECK(firstWasLimited && secondOk && device.paths == QStringList({"/board", "/board"}));
}

void testRefusals()
{
    for (int scenario = 0; scenario < 7; ++scenario) {
        VirtualDevice device;
        auto options = device.options();
        if (scenario == 0) device.board.insert("busy", true);
        if (scenario == 1) options.expectedDeviceId = "expected";
        if (scenario == 2) device.board.insert("board_connected", false);
        if (scenario == 3) device.board.insert("board_type", "other-board");
        if (scenario == 4) {
            QTcpServer portSelector;
            CHECK(portSelector.listen(QHostAddress::LocalHost, 0));
            options.listenPort = portSelector.serverPort();
            CHECK(options.listenPort != 0);
            portSelector.close();
            options.localAddress = "127.0.0.1";
            device.failTcp = true;
            device.noCallback = true;
            device.deferTcpReply = true;
        }
        if (scenario == 5) device.tcpStatus = {{"connected", true}, {"ip", "127.0.0.2"}};
        if (scenario == 6) { device.failCommand = true; device.traceDataEvents = true; device.dataClock.start(); }
        AcquisitionWifiSource source;
        source.open(options);
        if (scenario == 4) {
            // Hold the HTTP rejection until the advertised listener is proven
            // to occupy the selected address and port, without any callback.
            CHECK(waitFor([&] { return device.hasPendingTcpReply(); }));
            CHECK(device.tcpRequest.value("ip").toString() == options.localAddress);
            CHECK(device.tcpRequest.value("port").toInt() == int(options.listenPort));
            CHECK(!device.data);
            {
                QTcpServer occupied;
                CHECK(!occupied.listen(QHostAddress::LocalHost, options.listenPort));
                CHECK(occupied.serverError() == QAbstractSocket::AddressInUseError ||
                      occupied.serverError() == QAbstractSocket::SocketAccessError);
            }
            device.releaseTcpReply();
        }
        CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Faulted; }));
        const auto code = source.snapshot().error.code;
        if (scenario == 0) CHECK(code == "device.busy");
        if (scenario == 1) CHECK(code == "device.identity_mismatch");
        if (scenario == 2) CHECK(code == "device.rt_bci_incompatible");
        if (scenario == 3) CHECK(code == "device.rt_bci_incompatible");
        if (scenario == 4) CHECK(code == "device.rt_bci_http");
        if (scenario == 5) CHECK(code == "device.busy");
        if (scenario == 6) CHECK(code == "device.rt_bci_http");
        if (scenario < 4) CHECK(device.paths == QStringList{"/board"});
        if (scenario == 5) CHECK(device.paths == QStringList({"/board", "/tcp"}));
        CHECK(!device.paths.contains("/stream/start"));
        if (scenario == 4) {
            // Rebinding tests release directly. A late loopback connect can
            // connect to itself if its ephemeral local port equals the target.
            QTcpServer released;
            CHECK(released.listen(QHostAddress::LocalHost, options.listenPort));
            CHECK(released.serverPort() == options.listenPort);
            CHECK(!device.data);
        }
        if (scenario == 6) {
            CHECK(device.data);
            device.recordDataEvent("source Faulted");
            const bool disconnected = waitFor([&] { return !device.data || device.data->state() == QAbstractSocket::UnconnectedState; });
            if (!disconnected) {
                device.recordDataEvent("timeout");
                std::cerr << "testRefusals accepted callback: " << device.dataEvents.join(QLatin1Char('\n')).toStdString() << '\n';
            }
            CHECK(disconnected);
        }
    }
}

void testGenerationAndTimeouts()
{
    {
        VirtualDevice device;
        device.boardDelayMs = 200;
        AcquisitionWifiSource source;
        source.open(device.options());
        CHECK(waitFor([&] { return device.paths.contains("/board"); }));
        source.close();
        device.boardDelayMs = 0;
        source.open(device.options());
        CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Connected; }));
        QElapsedTimer delay; delay.start();
        while (delay.elapsed() < 250) { QCoreApplication::processEvents(); QThread::msleep(1); }
        CHECK(source.snapshot().connection == DeviceConnectionState::Connected);
        CHECK(device.paths.count("/stream/stop") == 0);
    }
    {
        VirtualDevice device;
        device.noCallback = true;
        AcquisitionWifiSource source;
        source.open(device.options());
        CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Faulted; }, 14000));
        CHECK(source.snapshot().error.code == "device.rt_bci_callback");
    }
    {
        VirtualDevice device;
        device.noStartData = true;
        AcquisitionWifiSource source;
        expectReady(source, device, device.options());
        const auto completion = source.snapshot().acquisitionCompletion;
        source.startAcquisition();
        CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Starting; }, 14000));
        CHECK(source.snapshot().samples == 0 && source.snapshot().error.code.isEmpty());
        CHECK(source.snapshot().acquisitionCompletion == completion);
        source.stopAcquisition();
        CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    }
}

void testDetectionAndFallback()
{
    {
        VirtualDevice device(8);
        AcquisitionWifiSource source;
        expectReady(source, device, device.options()); source.startAcquisition();
        CHECK(waitFor([&] { return source.captureContext().has_value(); }));
        CHECK(source.snapshot().totalChannels == 8);
        source.stopAcquisition(); CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
        device.channels = 16;
        source.startAcquisition();
        CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Starting; }));
        CHECK(source.snapshot().totalChannels == 0);
        CHECK(waitFor([&] { return source.captureContext().has_value(); }));
        CHECK(source.snapshot().totalChannels == 16);
        CHECK(source.captureContext()->capture.metadata.at("channel_report_mismatch") == "true");
        source.stopAcquisition(); CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    }
    for (int channels : {8, 16}) {
        VirtualDevice device(channels);
        device.board.insert("num_channels", channels == 16 ? 8 : 16);
        AcquisitionWifiSource source;
        auto options = device.options(); options.channels = channels == 16 ? 8 : 16;
        expectReady(source, device, options);
        CHECK(source.snapshot().totalChannels == 0);
        source.startAcquisition();
        CHECK(waitFor([&] { return source.captureContext().has_value(); }));
        const auto view = source.snapshot();
        const auto context = source.captureContext();
        CHECK(view.totalChannels == static_cast<unsigned>(channels));
        CHECK(view.warning.code == "device.channel_count_adjusted");
        CHECK(context->capture.streams.front().channels.size() == static_cast<unsigned>(channels));
        CHECK(context->capture.metadata.at("requested_channels") == std::to_string(options.channels));
        CHECK(context->capture.metadata.at("detected_channels") == std::to_string(channels));
        CHECK(context->capture.metadata.at("channel_layout_source") == "detected_from_wire");
        CHECK(context->capture.metadata.at("channel_report_mismatch") == "true");
        source.stopAcquisition(); CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    }
    {
        VirtualDevice device; device.noStartData = true;
        AcquisitionWifiSource source;
        auto options = device.options(); options.channels = 8;
        expectReady(source, device, options); source.startAcquisition();
        CHECK(waitFor([&] { return device.paths.contains("/stream/start"); }));
        QByteArray bytes;
        for (int i = 0; i < 200; ++i) bytes += halfFrame(7, 0, 0);
        device.data->write(bytes);
        CHECK(waitFor([&] { return source.captureContext().has_value(); }));
        const auto context = source.captureContext();
        CHECK(context->capture.streams.front().channels.size() == 8);
        CHECK(context->capture.metadata.at("channel_layout_source") == "user_forced");
        CHECK(context->capture.metadata.at("requested_channels") == "8");
        CHECK(context->capture.metadata.at("detected_channels") == "0");
        CHECK(source.snapshot().warning.code.isEmpty());
        source.stopAcquisition(); CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    }
    {
        VirtualDevice device; device.noStartData = true;
        AcquisitionWifiSource source;
        auto options = device.options(); options.channels = 16;
        expectReady(source, device, options); source.startAcquisition();
        CHECK(waitFor([&] { return device.paths.contains("/stream/start"); }));
        QByteArray bytes;
        for (int i = 0; i < 200; ++i) bytes += halfFrame(static_cast<std::uint8_t>(2 * i), i, 0);
        device.data->write(bytes);
        CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Faulted; }));
        CHECK(source.snapshot().error.code == "device.rt_bci_unrecognized_stream");
        CHECK(source.snapshot().error.params.value("reason") == "fallback_no_samples");
        CHECK(source.snapshot().totalChannels == 0);
    }
    {
        VirtualDevice device; device.noStartData = true;
        AcquisitionWifiSource source;
        expectReady(source, device, device.options()); source.startAcquisition();
        CHECK(waitFor([&] { return device.paths.contains("/stream/start"); }));
        CHECK(source.snapshot().connectionPhase == "detecting" && source.snapshot().totalChannels == 0);
        CHECK(!source.captureContext());
        QElapsedTimer elapsed; elapsed.start();
        device.data->write(QByteArray(12, '\0'));
        CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Faulted; }, 3400));
        const auto error = source.snapshot().error;
        CHECK(elapsed.elapsed() < 3400 && error.code == "device.rt_bci_unrecognized_stream");
        CHECK(error.params.value("received_bytes").toString() == "12");
        CHECK(error.params.value("reason") == "timeout");
        device.noStartData = false;
        device.paths.clear();
        expectReady(source, device, device.options()); source.startAcquisition();
        CHECK(waitFor([&] { return source.captureContext().has_value(); }));
        CHECK(source.snapshot().totalChannels == 8);
        source.stopAcquisition(); CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    }
}

void checkRaw(const Delivery& delivery, int channels)
{
    const auto& block = *delivery.block;
    const auto* raw = block.storage->data<std::int32_t>();
    CHECK(raw && block.storage->dtype() == DType::Int24 && block.channel_count == static_cast<unsigned>(channels));
    CHECK(block.sample_count <= 64 && block.host_received_monotonic_ns && block.timestamp_estimated);
    for (int ch = 0; ch < channels; ++ch)
        for (std::uint32_t i = 0; i < block.sample_count; ++i) {
            const auto group = static_cast<std::uint64_t>(((*raw)[i] - 100000) / 17);
            CHECK((*raw)[static_cast<std::size_t>(ch) * block.sample_count + i] == valueFor(group, ch));
        }
}

void testRaw(int channels, int rate, int reportedChannels = -1)
{
    VirtualDevice device(channels);
    if (reportedChannels < 0) reportedChannels = channels;
    device.board.insert("num_channels", reportedChannels);
    if (reportedChannels == 12) device.board.remove("board_type");
    device.tcpDelayMs = 100; // Device connects before /tcp completes.
    device.tcpStatus.insert("ip", "127.0.0.2"); // An old configured IP is not an active connection.
    AcquisitionWifiSource source;
    auto options = device.options();
    options.sampleRate = rate;
    device.emitDelimiters = false;
    expectReady(source, device, options);
    CHECK(device.tcpRequest.value("delimiter").isBool() && device.tcpRequest.value("delimiter").toBool());
    CHECK(device.tcpRequest.value("output") == "raw" && device.tcpRequest.value("latency") == 10000);
    const auto configCompletion = source.snapshot().configCompletion;
    const auto alternateRate = rate == 250 ? 500 : 250;
    source.configure(alternateRate);
    CHECK(waitFor([&] { return source.snapshot().configCompletion == configCompletion + 1; }));
    CHECK(device.commandRequest.value("command").toString() == (alternateRate == 250 ? "~6" : "~5"));
    source.configure(rate);
    CHECK(waitFor([&] { return source.snapshot().configCompletion == configCompletion + 2; }));
    CHECK(device.sampleRate == rate);
    const auto startCompletion = source.snapshot().acquisitionCompletion;
    source.startAcquisition();
    CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Streaming && (device.noStartData || source.captureContext().has_value()); }));
    CHECK(source.snapshot().acquisitionCompletion == startCompletion + 1);
    const auto context = source.captureContext();
    CHECK(context && context->device.weak_identity && context->device.firmware.empty());
    const auto& stream = context->capture.streams.at(0);
    CHECK(stream.dtype == DType::Int24 && stream.sample_index_source == SampleIndexSource::AdapterReceived);
    CHECK(stream.rate.numerator == static_cast<unsigned>(rate) && stream.rate.denominator == 1);
    CHECK(stream.clock.source == ClockSource::Estimated && stream.clock.epoch == ClockEpoch::SessionRelative);
    CHECK(context->capture.metadata.at("first_output_counter") == "254");
    CHECK(context->capture.metadata.at("configuration_verified") == "false");
    CHECK(context->capture.metadata.at("requested_channels") == "0");
    CHECK(context->capture.metadata.at("detected_channels") == std::to_string(channels));
    CHECK(context->capture.metadata.at("reported_channels") == std::to_string(reportedChannels));
    CHECK(context->capture.metadata.at("channel_layout_source") == "detected_from_wire");
    CHECK(context->capture.metadata.at("channel_report_mismatch") ==
          (reportedChannels != 0 && reportedChannels != channels ? "true" : "false"));
    CHECK(context->capture.metadata.at("prior_tcp_ownership") == "reported_disconnected_unverified");
    for (const auto& channel : stream.channels) CHECK(channel.unit == Unit::Count && channel.scale == 1.0);
    const auto consumer = context->hub->addConsumer(ConsumerKind::Recording, {8 * 1024 * 1024, 256});
    CHECK(consumer);
    CHECK(waitFor([&] { return (*consumer.value)->stats().accepted_samples >= 20; }));
    device.skipGroup();
    CHECK(waitFor([&] { return context->hub->stats().unknown_gaps >= 1; }));
    CHECK(context->hub->stats().known_missing_samples == 0);
    const auto stopCompletion = source.snapshot().acquisitionCompletion;
    source.stopAcquisition();
    CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    CHECK(source.snapshot().acquisitionCompletion == stopCompletion + 1);
    CHECK(!source.captureContext());
    auto deliveries = (*consumer.value)->snapshot();
    CHECK(!deliveries.empty());
    for (const auto& delivery : deliveries) checkRaw(delivery, channels);
    CHECK((*consumer.value)->stats().unknown_gaps >= 1);
    const auto gaps = (*consumer.value)->pendingGaps();
    CHECK(!gaps.empty());
    CHECK(gaps.back().reason == "rt_bci.end_boundary_unknown");
    const auto firstCapture = context->capture.capture_id;
    source.startAcquisition();
    CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Streaming && (device.noStartData || source.captureContext().has_value()); }));
    CHECK(source.captureContext() && source.captureContext()->capture.capture_id != firstCapture);
    source.stopAcquisition();
    CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    source.close();
    CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Disconnected; }));
    QFile log(source.snapshot().diagnosticLogPath); CHECK(log.open(QIODevice::ReadOnly));
    bool foundSelection = false;
    for (const auto& line : log.readAll().split('\n')) {
        const auto event = QJsonDocument::fromJson(line).object();
        if (event.value("kind") != "channel_selection") continue;
        CHECK(event.value("requested_channels") == 0);
        CHECK(event.value("reported_channels") == reportedChannels);
        CHECK(event.value("detected_channels") == channels);
        CHECK(event.value("channel_layout_source") == "detected_from_wire");
        CHECK(event.value("detect_frames").toInt() == 24);
        CHECK(event.value("detect_pairs").toInt() == (channels == 8 ? 0 : 12));
        foundSelection = true;
    }
    CHECK(foundSelection);
}

void testUnverifiedConfiguration()
{
    VirtualDevice device;
    device.tcpStatusCode = 404;
    AcquisitionWifiSource source;
    auto options = device.options();
    options.gain = 24;
    expectReady(source, device, options);
    source.startAcquisition();
    CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Streaming && (device.noStartData || source.captureContext().has_value()); }));
    const auto context = source.captureContext();
    CHECK(context && context->capture.metadata.at("prior_tcp_ownership") == "endpoint_unavailable");
    CHECK(context->capture.metadata.at("gain_source") == "user_declared_unverified");
    const auto& channel = context->capture.streams.at(0).channels.at(0);
    CHECK(channel.unit == Unit::Microvolt);
    CHECK(std::abs(channel.scale - 4.5 / (24 * 8388608.0) * 1000000.0) < 1e-15);
    CHECK(!source.snapshot().configurationVerified);
    source.stopAcquisition();
    CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
}

void testDataFaultAndSilence()
{
    VirtualDevice device;
    AcquisitionWifiSource source;
    expectReady(source, device, device.options());
    source.startAcquisition();
    CHECK(waitFor([&] { return source.captureContext().has_value(); }));
    const auto context = source.captureContext(); CHECK(context);
    auto consumer = context->hub->addConsumer(ConsumerKind::Recording, {8 * 1024 * 1024, 256}); CHECK(consumer);
    const auto before = context->hub->stats().accepted_samples;
    device.sendFault(); // Two valid groups, then an equal-ID frame: all three survive.
    CHECK(waitFor([&] { return context->hub->stats().accepted_samples >= before + 3; }));
    CHECK(source.snapshot().acquisition == DeviceAcquisitionState::Streaming && source.snapshot().error.code.isEmpty());
    auto bad = halfFrame(5, 17, 0); bad[32] = char(0xcf);
    device.data->write(bad + halfFrame(200, 18, 0) + halfFrame(201, 19, 0) + halfFrame(202, 20, 0));
    CHECK(waitFor([&] { return context->hub->stats().accepted_samples >= before + 6; }));
    QElapsedTimer pause; pause.start();
    while (pause.elapsed() < 3300) { QCoreApplication::processEvents(); QThread::msleep(1); }
    CHECK(source.snapshot().acquisition == DeviceAcquisitionState::Streaming);
    CHECK(context->hub->stats().unknown_gaps >= 1 && context->hub->stats().known_missing_samples == 0);
    device.sendGroups(5);
    CHECK(waitFor([&] { return context->hub->stats().accepted_samples >= before + 11; }));
    source.stopAcquisition(); CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    // An incomplete final packet is logged, not an acquisition failure.
    source.startAcquisition(); CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Streaming && (device.noStartData || source.captureContext().has_value()); }));
    device.samples.stop(); device.data->write(QByteArray(5, 'x'));
    CHECK(waitFor([&] { return device.data->bytesToWrite() == 0; }));
    source.stopAcquisition(); CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    CHECK(source.snapshot().error.code.isEmpty());
    for (const auto& delivery : (*consumer.value)->snapshot()) checkRaw(delivery, 8);
    device.disconnectData(); CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Faulted; }));
    CHECK(source.snapshot().error.code == "device.disconnected");
    const auto logPath = source.snapshot().error.params.value("diagnostic_log").toString();
    QFile log(logPath); CHECK(log.open(QIODevice::ReadOnly)); const auto bytes = log.readAll();
    CHECK(bytes.contains("duplicate_counter") && bytes.contains("frame_resync") && bytes.contains("raw_hex"));
    CHECK(bytes.contains("receive_idle") && bytes.contains("truncated_frame"));
}

void testSignalStatistics()
{
    QTemporaryDir output;
    const auto previous = qgetenv("BF_ACQUISITION_LOG_DIRECTORY");
    qputenv("BF_ACQUISITION_LOG_DIRECTORY", output.path().toUtf8());
    VirtualDevice device; device.noStartData = true;
    AcquisitionWifiSource source;
    expectReady(source, device, device.options()); source.startAcquisition();
    CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Starting; }));
    QByteArray wire;
    for (int i = 0; i < 24; ++i) {
        QByteArray frame(33, '\0'); frame[0] = char(0xa0); frame[1] = char(i); frame[32] = char(0xc0);
        for (int ch = 0; ch < 8; ++ch) {
            const std::int32_t value = ch == 0 ? 8388607 : ch == 1 ? -8388608 :
                ch == 2 ? 5000000 + (i % 2 ? 2 : -2) : 0;
            const auto bits = static_cast<std::uint32_t>(value) & 0xffffff;
            frame[2 + ch * 3] = char(bits >> 16); frame[3 + ch * 3] = char(bits >> 8); frame[4 + ch * 3] = char(bits);
        }
        wire += frame;
    }
    device.data->write(wire);
    CHECK(waitFor([&] { return source.snapshot().samples == 24; }));
    source.stopAcquisition(); CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
    const QDir logs(output.path() + "/BoundFree-Acquisition-Logs");
    const auto names = logs.entryList({"*.jsonl"}, QDir::Files); CHECK(names.size() == 1);
    QFile log(logs.filePath(names.front())); CHECK(log.open(QIODevice::ReadOnly));
    bool found = false;
    for (const auto& line : log.readAll().split('\n')) {
        const auto event = QJsonDocument::fromJson(line).object();
        if (event.value("kind") != "signal_stats") continue;
        const auto channels = event.value("channels").toArray();
        CHECK(event.value("samples") == "24" && event.value("unit") == "raw_sample" && channels.size() == 8);
        for (int ch : {0, 1}) {
            const auto stats = channels[ch].toObject();
            CHECK(stats.value("flat").toBool() && stats.value("rail_fraction").toDouble() == 1.0);
            CHECK(stats.value("peak_to_peak").toDouble() == 0 && stats.value("changes") == "0");
        }
        const auto ripple = channels[2].toObject();
        CHECK(!ripple.value("flat").toBool() && ripple.value("rail_fraction").toDouble() == 0);
        CHECK(ripple.value("peak_to_peak").toDouble() == 4 && ripple.value("changes") == "23");
        CHECK(std::abs(ripple.value("mean").toDouble() - 5000000) < 1e-9);
        CHECK(std::abs(ripple.value("stddev").toDouble() - 2) < 1e-9);
        found = true;
    }
    CHECK(found);
    qputenv("BF_ACQUISITION_LOG_DIRECTORY", previous);
}

void testUnansweredControls()
{
    {
        VirtualDevice device;
        AcquisitionWifiSource source;
        expectReady(source, device, device.options());
        device.rejectCommand = true;
        const auto completion = source.snapshot().configCompletion;
        source.configure(500);
        CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Faulted; }));
        CHECK(source.snapshot().configCompletion == completion + 1);
        CHECK(source.snapshot().error.code == "device.rt_bci_http");
        CHECK(!source.snapshot().configurationVerified);
    }
    {
        VirtualDevice device;
        AcquisitionWifiSource source;
        expectReady(source, device, device.options());
        device.commandDelayMs = 500;
        const auto completion = source.snapshot().configCompletion;
        source.configure(500);
        CHECK(waitFor([&] { return source.snapshot().configurationInProgress; }));
        source.close();
        CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Disconnected; }));
        CHECK(source.snapshot().configCompletion == completion + 1);
    }
    {
        VirtualDevice device;
        AcquisitionWifiSource source;
        expectReady(source, device, device.options());
        device.startDelayMs = 200;
        const auto completion = source.snapshot().acquisitionCompletion;
        source.startAcquisition();
        CHECK(waitFor([&] { return source.captureContext().has_value(); }));
        source.stopAcquisition();
        CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }));
        CHECK(source.snapshot().acquisitionCompletion == completion + 2);
    }
    {
        VirtualDevice device;
        AcquisitionWifiSource source;
        expectReady(source, device, device.options());
        device.startDelayMs = 12000;
        source.startAcquisition();
        CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Faulted; }, 14000));
        CHECK(source.snapshot().error.code == "device.rt_bci_http");
        CHECK(!source.captureContext() && source.snapshot().samples == 0);
        CHECK(device.paths.count("/stream/start") == 1);
    }
    {
        VirtualDevice device;
        AcquisitionWifiSource source;
        expectReady(source, device, device.options());
        source.startAcquisition();
        CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Streaming && (device.noStartData || source.captureContext().has_value()); }));
        const auto context = source.captureContext();
        CHECK(context);
        auto consumer = context->hub->addConsumer(ConsumerKind::Recording, {1024 * 1024, 64});
        CHECK(consumer);
        device.stopDelayMs = 12000;
        source.stopAcquisition();
        CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Faulted; }, 14000));
        CHECK(source.snapshot().error.code == "device.rt_bci_http");
        CHECK(device.paths.count("/stream/stop") == 1); // Exactly one requested stop, no initial stop.
        CHECK((*consumer.value)->stats().unknown_gaps > 0);
    }
}

void testReferencePostResponses()
{
    const std::vector<QByteArray> good{
        "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\ncontent-length: 2\r\nconnection: close\r\n\r\n{}",
        "HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;ext=yes\r\n{\r\n1\r\n}\r\n0\r\nX-Check: yes\r\n\r\n",
        "HTTP/1.0 204 No Content\r\n\r\n"
    };
    for (const auto& response : good) {
        VirtualDevice device;
        device.commandWireReply = response;
        device.replyFragment = 3;
        AcquisitionWifiSource source;
        expectReady(source, device, device.options());
        CHECK(device.postWire.size() == 4);
        CHECK(device.postWire[0].startsWith("POST /tcp HTTP/1.0\r\nHost: "));
        CHECK(device.postWire[1].startsWith("POST /command HTTP/1.0\r\nHost: "));
        CHECK(!device.paths.contains("/stream/start"));
    }
    const std::vector<QByteArray> bad{
        "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n{",
        "HTTP/1.0 200 OK\r\nContent-Length: 65537\r\n\r\n",
        "HTTP/1.0 200 OK\r\nContent-Length: 2\r\nContent-Length: 2\r\n\r\n{}",
        "HTTP/1.0 200 OK\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nX\r\n{}\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}!\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{",
        "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n{}extra",
        "bad status\r\n\r\n{}",
        "HTTP/1.0 204 No Content\r\nContent-Length: 2\r\n\r\n{}",
        QByteArray("HTTP/1.0 200 OK\r\nX-Huge: ") + QByteArray(17000, 'x')
    };
    for (const auto& response : bad) {
        VirtualDevice device;
        device.commandWireReply = response;
        AcquisitionWifiSource source;
        source.open(device.options());
        CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Faulted; }));
        const auto error = source.snapshot().error;
        CHECK(error.code == "device.rt_bci_http");
        CHECK(error.params.value("request_http_version") == "1.0");
        CHECK(error.params.value("path") == "/command");
        CHECK(device.paths.count("/command") == 1 && !device.paths.contains("/stream/start"));
    }
}

void testFailureDiagnostics()
{
    for (int scenario = 0; scenario < 10; ++scenario) {
        VirtualDevice device;
        if (scenario == 0) device.boardStatus = 500;
        if (scenario == 1) device.tcpStatusCode = 500;
        if (scenario == 2) device.tcpStatusCode = 502;
        if (scenario == 3) { device.tcpPostStatus = 504; device.noCallback = true; }
        if (scenario == 4) device.commandStatus = 502;
        if (scenario == 5) device.rejectCommand = true;
        if (scenario == 6) { device.noCallback = true; device.tcpDelayMs = 12000; }
        if (scenario == 7) device.commandDelayMs = 12000;
        if (scenario == 8 || scenario == 9) {
            device.tcpPostStatus = 400; device.noCallback = true;
            device.tcpPostBody = scenario == 8 ? "{\"error\":\"Invalid delimiter\",\"password\":\"must-not-copy\"}" : "token=must-not-copy";
        }
        AcquisitionWifiSource source;
        source.open(device.options());
        CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Faulted; }, 14000));
        const auto view = source.snapshot();
        const auto params = view.error.params;
        const QString path = scenario == 0 ? "/board" : scenario == 2 ? "/tcp" :
            scenario == 4 || scenario == 5 || scenario == 7 ? "/command" : "/tcp";
        CHECK(view.error.code == (scenario == 3 ? "device.rt_bci_callback" : "device.rt_bci_http"));
        CHECK(view.acquisition == DeviceAcquisitionState::Idle);
        CHECK(params.value("path").toString() == path);
        CHECK(params.value("method").toString() == (scenario <= 2 ? "GET" : "POST"));
        CHECK(params.value("host") == "127.0.0.1");
        CHECK(!params.value("phase").toString().isEmpty());
        CHECK(params.contains("elapsed_ms") && params.contains("network_error"));
        if (scenario == 3) {
            CHECK(params.value("http_status") == 504);
            CHECK(params.value("local_address") == "127.0.0.1");
            CHECK(params.value("listen_port").toInt() == device.tcpRequest.value("port").toInt());
        }
        if (scenario == 4) CHECK(params.value("http_status") == 502);
        if (scenario == 5) CHECK(params.value("reason") == "rejected");
        if (scenario == 6 || scenario == 7) CHECK(params.value("reason") == "timeout");
        if (scenario >= 8) {
            CHECK(params.value("http_status") == 400);
            CHECK(params.value("response_error") == (scenario == 8 ? "Invalid delimiter" : "[redacted]"));
            CHECK(params.value("tcp_request").toObject() == device.tcpRequest);
            CHECK(!QJsonDocument(params).toJson().contains("must-not-copy"));
        }
        CHECK(!device.paths.contains("/stream/start"));
        CHECK(device.paths.count("/command") <= 1);
        CHECK(device.paths.count("/tcp") <= 2); // One GET, at most one POST.
    }
    VirtualDevice device;
    AcquisitionWifiSource source;
    expectReady(source, device, device.options());
    device.startStatus = 503;
    source.startAcquisition();
    CHECK(waitFor([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Faulted; }));
    CHECK(source.snapshot().samples == 0);
    CHECK(source.snapshot().error.params.value("path") == "/stream/start");
    CHECK(source.snapshot().error.params.value("http_status") == 503);
    CHECK(device.paths.count("/stream/start") == 1);
}

void verifyCapture(const QString& path, int channels, int reportedChannels)
{
    QFile file(QDir(path).filePath("manifest.json"));
    CHECK(file.open(QIODevice::ReadOnly));
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    CHECK(manifest.value("status").toString() == "partial");
    const auto meta = manifest.value("acquisition_metadata").toObject();
    CHECK(meta.value("first_output_counter").toString() == "254");
    CHECK(meta.value("configuration_verified").toString() == "false");
    CHECK(meta.value("requested_channels").toString() == "0");
    CHECK(meta.value("detected_channels").toString() == QString::number(channels));
    CHECK(meta.value("reported_channels").toString() == QString::number(reportedChannels));
    CHECK(meta.value("channel_layout_source").toString() == "detected_from_wire");
    CHECK(meta.value("channel_report_mismatch").toString() ==
          (reportedChannels != 0 && reportedChannels != channels ? "true" : "false"));
    CHECK(meta.value("sample_index_meaning").toString() == "adapter_received_ordinal_not_device_counter");
    CHECK(meta.value("gain_source").toString() == "unknown_raw_counts");
    const auto descriptor = manifest.value("streams").toArray().at(0).toObject().value("descriptor").toObject();
    CHECK(descriptor.value("dtype").toString() == "int24");
    CHECK(descriptor.value("channels").toArray().size() == channels);
    for (const auto& channel : descriptor.value("channels").toArray()) {
        CHECK(channel.toObject().value("unit").toString() == "count");
        CHECK(channel.toObject().value("scale").toDouble() == 1.0);
    }
    QFile events(QDir(path).filePath("events.jsonl"));
    CHECK(events.open(QIODevice::ReadOnly));
    QJsonObject expectedIdentity;
    for (const char* key : {"device_id", "boot_id", "session_id", "capture_id", "generation", "stream_id", "descriptor_revision"}) {
        const auto value = descriptor.value(QLatin1String(key));
        CHECK(!value.isUndefined() && !value.isNull());
        expectedIdentity.insert(QLatin1String(key), value);
    }
    const QString expectedTail = "rt_bci.end_boundary_unknown";
    QStringList gapDiagnostics;
    bool foundAdapterTail = false;
    for (const auto& line : events.readAll().split('\n')) {
        if (line.trimmed().isEmpty()) continue;
        const auto parsed = QJsonDocument::fromJson(line);
        CHECK(parsed.isObject());
        const auto event = parsed.object();
        if (event.value("kind").toString() != "data.gap") continue;
        const auto params = event.value("params").toObject();
        const auto detection = params.value("detection").toString();
        const auto reason = params.value("reason").toString();
        const bool identityMatches = event.value("stream").isObject() && event.value("stream").toObject() == expectedIdentity;
        gapDiagnostics.push_back(detection + ":" + reason + (identityMatches ? ":identity_match" : ":identity_mismatch"));
        if (detection == "adapter" && reason == expectedTail && identityMatches) foundAdapterTail = true;
    }
    if (!foundAdapterTail)
        std::cerr << "RT-BCI " << channels << "-channel gap events (detection:reason): "
                  << gapDiagnostics.join(", ").toStdString() << "; expected adapter:" << expectedTail.toStdString() << '\n';
    CHECK(foundAdapterTail);
    // Read the real sidecar's HDF5 output and independently reconstruct every
    // expected channel value using the recorded absolute sample indices.
    const QString program = qEnvironmentVariable("BF_PYTHON_EXECUTABLE", "python3");
    const QString script = QStringLiteral(
        "import h5py,json,sys\n"
        "p=sys.argv[1]; n=int(sys.argv[2]); m=json.load(open(p+'/manifest.json',encoding='utf-8'))\n"
        "d=m['streams'][0]['descriptor']; f=h5py.File(p+'/signals.h5','r'); g=f['streams'][d['session_id']][str(d['stream_id'])]\n"
        "a=g['samples']; blocks=g['blocks']; assert len(a)>0 and a.shape[1]==n\n"
        "for b in blocks:\n"
        " first=int(b['sample_index']); off=int(b['row_offset']); count=int(b['sample_count'])\n"
        " for i in range(count):\n"
        "  for c in range(n):\n"
        "   group=(int(a[off+i,0])-100000)//17; v=100000+group*17+c*1000; assert int(a[off+i,c])==(-v if c%2 else v)\n");
    QProcess verify;
    verify.start(program, {"-c", script, path, QString::number(channels)});
    CHECK(verify.waitForFinished(30000));
    if (verify.exitCode() != 0) std::cerr << verify.readAllStandardError().constData() << std::endl;
    CHECK(verify.exitStatus() == QProcess::NormalExit && verify.exitCode() == 0);
}

void testControllerAndRecorder(const QString& repoRoot, int channels, int reportedChannels = -1)
{
    VirtualDevice device(channels);
    if (reportedChannels < 0) reportedChannels = channels;
    device.board.insert("num_channels", reportedChannels);
    QTemporaryDir output;
    CHECK(output.isValid());
    VersionInfo version; version.ipcProtocolVersion = 1;
    AcquisitionController controller(repoRoot, version);
    controller.connectWifi(device.options());
    CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Connected; }));
    CHECK(controller.snapshot().acquisition == DeviceAcquisitionState::Idle);
    CHECK(!device.paths.contains("/stream/start"));
    controller.startAcquisition();
    CHECK(waitFor([&] { return controller.snapshot().acquisition == DeviceAcquisitionState::Streaming && controller.snapshot().totalChannels == static_cast<unsigned>(channels); }));
    controller.startRecording(output.path());
    CHECK(waitFor([&] { return controller.snapshot().recording.state == CaptureRecordState::Recording ||
                               controller.snapshot().recording.state == CaptureRecordState::Failed; }, 15000));
    CHECK(controller.snapshot().recording.state == CaptureRecordState::Recording);
    CHECK(waitFor([&] { return controller.snapshot().recording.writtenSamples >= 20; }, 15000));
    if (channels == 8) {
        const auto written = controller.snapshot().recording.writtenSamples;
        device.sendFault(); device.sendGroups(10);
        CHECK(waitFor([&] { return controller.snapshot().recording.writtenSamples >= written + 10; }, 15000));
        CHECK(controller.snapshot().acquisition == DeviceAcquisitionState::Streaming);
    }
    controller.stopAcquisition();
    CHECK(waitFor([&] { return controller.snapshot().acquisition == DeviceAcquisitionState::Idle; }, 5000));
    CHECK(waitFor([&] { const auto s = controller.snapshot().recording.state;
                        return s == CaptureRecordState::Partial || s == CaptureRecordState::Saved || s == CaptureRecordState::Failed; }, 30000));
    const auto recording = controller.snapshot().recording;
    CHECK(recording.state == CaptureRecordState::Partial);
    verifyCapture(recording.path, channels, reportedChannels);
}

void testControllerDisconnectRace(const QString& repoRoot)
{
    VirtualDevice device;
    VersionInfo version; version.ipcProtocolVersion = 1;
    AcquisitionController controller(repoRoot, version);
    controller.connectWifi(device.options());
    CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Connected; }));
    const auto before = controller.snapshot();
    const int commands = device.paths.count("/command");
    const int starts = device.paths.count("/stream/start");

    // No event processing between these calls: the source close is still queued
    // or its completion notification has not reached the controller.
    controller.disconnectDevice();
    CHECK(controller.snapshot().connection == DeviceConnectionState::Connecting);
    CHECK(controller.hasActiveTasks());
    controller.startAcquisition();
    controller.configureSampleRate(500);
    CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Disconnected; }));
    const auto after = controller.snapshot();
    CHECK(after.error.code.isEmpty());
    CHECK(after.disconnectionCompletion == before.disconnectionCompletion + 1);
    CHECK(!controller.hasActiveTasks());
    CHECK(device.paths.count("/command") == commands);
    CHECK(device.paths.count("/stream/start") == starts);
}

void testControllerShutdownDuringConfiguration(const QString& repoRoot)
{
    VirtualDevice device;
    VersionInfo version; version.ipcProtocolVersion = 1;
    AcquisitionController controller(repoRoot, version);
    controller.connectWifi(device.options());
    CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Connected; }));
    device.commandDelayMs = 200;
    controller.configureSampleRate(500);
    CHECK(controller.snapshot().configurationInProgress && controller.hasActiveTasks());
    CHECK(waitFor([&] { return device.paths.count("/command") == 4; }));

    int shutdownSignals = 0;
    bool shutdownSucceeded = false;
    QObject::connect(&controller, &AcquisitionController::shutdownFinished, &controller, [&](bool success) {
        ++shutdownSignals; shutdownSucceeded = success;
    });
    controller.requestShutdown();
    CHECK(shutdownSignals == 0);
    QElapsedTimer early; early.start();
    while (early.elapsed() < 80) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    CHECK(shutdownSignals == 0);
    CHECK(waitFor([&] { return shutdownSignals == 1; }, 3000));
    CHECK(shutdownSucceeded);
    CHECK(!controller.snapshot().configurationInProgress && controller.snapshot().sampleRate == 500);
}

void testControllerShutdownFailures(const QString& repoRoot)
{
    {
        VirtualDevice device;
        VersionInfo version; version.ipcProtocolVersion = 1;
        AcquisitionController controller(repoRoot, version);
        controller.connectWifi(device.options());
        CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Connected; }));
        controller.startAcquisition();
        CHECK(waitFor([&] { return controller.snapshot().acquisition == DeviceAcquisitionState::Streaming; }));
        device.failStop = true;
        int shutdownSignals = 0;
        bool shutdownSucceeded = true;
        QObject::connect(&controller, &AcquisitionController::shutdownFinished, &controller, [&](bool success) {
            ++shutdownSignals; shutdownSucceeded = success;
        });
        controller.requestShutdown();
        CHECK(waitFor([&] { return shutdownSignals == 1; }, 5000));
        CHECK(!shutdownSucceeded);
        CHECK(controller.snapshot().acquisition == DeviceAcquisitionState::Faulted);
        CHECK(controller.snapshot().error.code == "device.rt_bci_http");
        CHECK(controller.snapshot().recording.state == CaptureRecordState::Off);
        CHECK(device.paths.count("/stream/stop") == 1);
    }
    {
        VirtualDevice device;
        QTemporaryDir output;
        CHECK(output.isValid());
        VersionInfo version; version.ipcProtocolVersion = 1;
        AcquisitionController controller(repoRoot, version);
        controller.connectWifi(device.options());
        CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Connected; }));
        controller.startAcquisition();
        CHECK(waitFor([&] { return controller.snapshot().acquisition == DeviceAcquisitionState::Streaming; }));
        controller.startRecording(output.path());
        CHECK(waitFor([&] { const auto r = controller.snapshot().recording;
            return r.state == CaptureRecordState::Failed || (r.state == CaptureRecordState::Recording && r.writtenSamples >= 20);
        }, 15000));
        CHECK(controller.snapshot().recording.state == CaptureRecordState::Recording);
        device.stopDelayMs = 1000;
        int shutdownSignals = 0;
        bool shutdownSucceeded = true;
        QObject::connect(&controller, &AcquisitionController::shutdownFinished, &controller, [&](bool success) {
            ++shutdownSignals; shutdownSucceeded = success;
        });
        controller.requestShutdown();
        CHECK(waitFor([&] { return device.paths.count("/stream/stop") == 1; }));
        CHECK(shutdownSignals == 0);
        device.disconnectData();
        CHECK(waitFor([&] { return shutdownSignals == 1; }, 30000));
        const auto result = controller.snapshot();
        CHECK(!shutdownSucceeded && result.acquisition == DeviceAcquisitionState::Faulted);
        CHECK(result.error.code == "device.disconnected");
        CHECK(result.recording.state == CaptureRecordState::Partial);
        QFile manifest(QDir(result.recording.path).filePath("manifest.json"));
        CHECK(manifest.open(QIODevice::ReadOnly));
        CHECK(QJsonDocument::fromJson(manifest.readAll()).object().value("status").toString() == "partial");
        CHECK(device.paths.count("/stream/stop") == 1);
    }
}

void testControllerRetryWithStaleNotification(const QString& repoRoot)
{
    VirtualDevice device;
    device.board.insert("board_type", "Ganglion");
    VersionInfo version; version.ipcProtocolVersion = 1;
    AcquisitionController controller(repoRoot, version);
    controller.connectWifi(device.options());
    CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Faulted; }));
    auto* source = controller.findChild<AcquisitionWifiSource*>();
    CHECK(source);
    const auto oldRevision = source->snapshot().connectionRevision;
    CHECK(source->snapshot().connection == DeviceConnectionState::Faulted);
    device.board.insert("board_type", "Cyton");
    device.boardDelayMs = 200;

    bool armed = false, injecting = false, injected = false, observedOld = false;
    bool wrongProjection = false, retryTracking = false, unexpectedFault = false;
    QObject::connect(source, &AcquisitionSource::stateChanged, &controller, [&] {
        if (injecting && source->snapshot().connectionRevision == oldRevision &&
            source->snapshot().connection == DeviceConnectionState::Faulted) observedOld = true;
    });
    QObject::connect(&controller, &AcquisitionController::stateChanged, &controller, [&] {
        if (retryTracking && controller.snapshot().connection == DeviceConnectionState::Faulted) unexpectedFault = true;
        if (!armed || injecting) return;
        // selectSource sets its pending projection and then emits stateChanged
        // via clearResult, before it has queued the new source open. Dispatch
        // an old, explicitly queued notification at precisely this boundary.
        armed = false;
        injecting = true;
        if (controller.snapshot().connection != DeviceConnectionState::Connecting ||
            source->snapshot().connectionRevision != oldRevision ||
            source->snapshot().connection != DeviceConnectionState::Faulted) wrongProjection = true;
        QCoreApplication::sendPostedEvents(source, QEvent::MetaCall);
        if (controller.snapshot().connection != DeviceConnectionState::Connecting) wrongProjection = true;
        injected = true;
        injecting = false;
    });
    QMetaObject::invokeMethod(source, [source] { emit source->stateChanged(); }, Qt::QueuedConnection);
    armed = true;
    retryTracking = true;
    controller.connectWifi(device.options());
    CHECK(injected && observedOld && !wrongProjection && !unexpectedFault);
    CHECK(controller.snapshot().connection == DeviceConnectionState::Connecting && controller.hasActiveTasks());
    CHECK(waitFor([&] { return device.paths.count("/board") == 2; }));
    CHECK(controller.snapshot().connection == DeviceConnectionState::Connecting);
    CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Connected; }));
    retryTracking = false;
    CHECK(!unexpectedFault && controller.snapshot().connectionRevision == oldRevision + 1);
}

void testControllerSynchronousCancelBeforeOpen(const QString& repoRoot)
{
    for (const bool initiallyFaulted : {false, true}) {
        for (const bool requestShutdown : {false, true}) {
            VirtualDevice device;
            VersionInfo version; version.ipcProtocolVersion = 1;
            AcquisitionController controller(repoRoot, version);
            auto* source = controller.findChild<AcquisitionWifiSource*>();
            CHECK(source);
            if (initiallyFaulted) {
                device.board.insert("board_type", "Ganglion");
                controller.connectWifi(device.options());
                CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Faulted; }));
                device.board.insert("board_type", "Cyton");
            }
            const auto expectedRawState = initiallyFaulted ? DeviceConnectionState::Faulted : DeviceConnectionState::Disconnected;
            CHECK(source->snapshot().connection == expectedRawState);
            const auto initialRevision = source->snapshot().connectionRevision;
            const auto initialTicket = source->lastCloseRequest();
            const auto pathsBefore = device.paths;
            bool armed = true, called = false, sawExpectedRaw = false, workerClosed = false;
            int sourceNotificationsDuringListener = 0, shutdownSignals = 0;
            bool shutdownSucceeded = false, shutdownFiredDuringListener = false, inListener = false;
            QObject::connect(source, &AcquisitionSource::stateChanged, &controller, [&] {
                if (inListener) ++sourceNotificationsDuringListener;
            });
            QObject::connect(&controller, &AcquisitionController::shutdownFinished, &controller, [&](bool success) {
                ++shutdownSignals; shutdownSucceeded = success;
                if (inListener) shutdownFiredDuringListener = true;
            });
            QObject::connect(&controller, &AcquisitionController::stateChanged, &controller, [&] {
                if (!armed || controller.snapshot().connection != DeviceConnectionState::Connecting) return;
                // clearResult emits synchronously, before connectWifi can queue open.
                armed = false;
                called = true;
                inListener = true;
                sawExpectedRaw = source->snapshot().connection == expectedRawState;
                if (requestShutdown) controller.requestShutdown();
                else controller.disconnectDevice();
                const auto closeTicket = source->lastCloseRequest();
                workerClosed = closeTicket == initialTicket + 1 && waitWithoutGuiEvents([&] {
                    return source->snapshot().disconnectionCompletion >= closeTicket &&
                           source->snapshot().connection == DeviceConnectionState::Disconnected;
                });
                inListener = false;
            });
            controller.connectWifi(device.options());
            CHECK(called && sawExpectedRaw && workerClosed);
            CHECK(!shutdownFiredDuringListener && sourceNotificationsDuringListener == 0);
            CHECK(source->lastCloseRequest() == initialTicket + 1);

            // A second, untracked close is a worker FIFO fence: even an
            // incorrectly queued open would advance the revision before it.
            source->close();
            const auto fence = source->lastCloseRequest();
            CHECK(fence == initialTicket + 2);
            CHECK(waitWithoutGuiEvents([&] { return source->snapshot().disconnectionCompletion >= fence; }));
            CHECK(source->snapshot().connectionRevision == initialRevision);
            CHECK(device.paths == pathsBefore);
            CHECK(waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Disconnected; }));
            CHECK(device.paths == pathsBefore);
            if (requestShutdown) {
                CHECK(waitFor([&] { return shutdownSignals == 1; }));
                // The prior raw fault must survive close projection and a
                // coalesced fault-to-disconnected notification.
                CHECK(shutdownSucceeded == !initiallyFaulted);
            } else {
                CHECK(shutdownSignals == 0);
            }
        }
    }
}

void testControllerNestedCancelRetry(const QString& repoRoot)
{
    VirtualDevice device;
    VersionInfo version; version.ipcProtocolVersion = 1;
    AcquisitionController controller(repoRoot, version);
    auto* source = controller.findChild<AcquisitionWifiSource*>();
    CHECK(source);
    const auto initialRevision = source->snapshot().connectionRevision;
    auto retry = device.options();
    retry.host = "not-an-ip"; // Fails on the worker without requiring GUI events.
    bool called = false, cancelled = false, retryFailed = false;
    QObject::connect(&controller, &AcquisitionController::stateChanged, &controller, [&] {
        if (called || controller.snapshot().connection != DeviceConnectionState::Connecting) return;
        called = true;
        controller.disconnectDevice();
        // Deliver the close completion before returning to the outer selectSource.
        cancelled = waitFor([&] { return controller.snapshot().connection == DeviceConnectionState::Disconnected; });
        if (!cancelled) return;
        controller.connectWifi(retry);
        // Leave the retry's GUI notification queued until the outer call returns.
        retryFailed = waitWithoutGuiEvents([&] {
            const auto state = source->snapshot();
            return state.connectionRevision == initialRevision + 1 &&
                   state.connection == DeviceConnectionState::Faulted && state.error.code == "device.invalid_endpoint";
        });
    });
    controller.connectWifi(device.options()); // This outer request is cancelled.
    CHECK(called && cancelled && retryFailed);

    // FIFO fence: a mistakenly queued outer open advances the revision before close.
    source->close();
    const auto fence = source->lastCloseRequest();
    CHECK(waitWithoutGuiEvents([&] { return source->snapshot().disconnectionCompletion >= fence; }));
    CHECK(source->snapshot().connectionRevision == initialRevision + 1);
    CHECK(device.paths.isEmpty());
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir logs; qputenv("BF_ACQUISITION_LOG_DIRECTORY", logs.path().toUtf8());
    try {
        CHECK(argc > 1);
        const QString root = QString::fromLocal8Bit(argv[1]);
        if (argc > 2 && QString::fromLocal8Bit(argv[2]) == "--channel-selection-only") {
            for (int channels : {8, 16}) {
                for (int rate : {250, 500, 1000}) testRaw(channels, rate, channels == 16 ? 8 : 16);
                testRaw(channels, 250, 0);
                testControllerAndRecorder(root, channels, channels == 16 ? 8 : 16);
            }
            std::cout << "RTBCI_CHANNEL_SELECTION PASS 8_and_16_layout raw_values all_rates unknown_report log recording_readback\n";
            return 0;
        }
        if (argc > 2 && QString::fromLocal8Bit(argv[2]) == "--http-wire-only") {
            VirtualDevice device;
            AcquisitionWifiSource source;
            expectReady(source, device, device.options());
            for (const auto& wire : device.postWire) std::cout << wire.toStdString() << "\n";
            source.close();
            CHECK(waitFor([&] { return source.snapshot().connection == DeviceConnectionState::Disconnected; }));
            std::cout << "HTTP_WIRE PASS\n";
            return 0;
        }
        testProbe();
        testReentrantOverlimit();
        testRefusals();
        testGenerationAndTimeouts();
        testDetectionAndFallback();
        for (int channels : {8, 16})
            for (int rate : {250, 500, 1000}) testRaw(channels, rate);
        for (int channels : {8, 16}) {
            for (int rate : {250, 500, 1000}) testRaw(channels, rate, channels == 16 ? 8 : 16);
            testRaw(channels, 250, 0);
            testRaw(channels, 250, 12);
            testControllerAndRecorder(root, channels, channels == 16 ? 8 : 16);
        }
        testUnverifiedConfiguration();
        testDataFaultAndSilence();
        testSignalStatistics();
        testUnansweredControls();
        testFailureDiagnostics();
        testReferencePostResponses();
        testControllerAndRecorder(root, 8);
        testControllerAndRecorder(root, 16);
        testControllerDisconnectRace(root);
        testControllerShutdownDuringConfiguration(root);
        testControllerShutdownFailures(root);
        testControllerRetryWithStaleNotification(root);
        testControllerSynchronousCancelBeforeOpen(root);
        testControllerNestedCancelRetry(root);
        std::cout << "ACQUISITION_WIFI_SOURCE_TEST PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RT-BCI virtual Wi-Fi regression failed: " << error.what() << '\n';
        return 1;
    }
}
