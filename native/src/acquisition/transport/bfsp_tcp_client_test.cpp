#include "bfsp_tcp_client.h"
#include "../protocol/codec.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtEndian>

#include <cstdio>
#include <functional>
#include <memory>

using bf::transport::BfspTcpClient;

namespace {
using MessageType = bf::bfsp::MessageType;
using State = BfspTcpClient::State;
const QString sessionId = QStringLiteral("11111111-1111-4111-8111-111111111111");
const QString captureId = QStringLiteral("22222222-2222-4222-8222-222222222222");

QString requestId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower(); }

// In-process wire peer keeps fault injection local to this test. In particular,
// direct signal connections exercise the same reentry used by TcpSource.
class ScriptedPeer final : public QObject {
public:
    explicit ScriptedPeer(QCoreApplication& app) : QObject(&app)
    {
        clock.start();
        heartbeat.setInterval(1000);
        connect(&heartbeat, &QTimer::timeout, this, [this] { sendJson(MessageType::Heartbeat, {}); });
        connect(&server, &QTcpServer::newConnection, this, [this] {
            heartbeat.stop();
            peer = server.nextPendingConnection();
            parser = std::make_unique<bf::bfsp::StreamParser>();
            sequences.clear();
            auto* socket = peer;
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                if (peer != socket) return;
                const auto bytes = socket->readAll();
                const auto parsed = parser->feed(bytes, clock.elapsed());
                if (parsed.faulted || parsed.consumed != bytes.size()) {
                    qCritical() << "scripted peer rejected input"; QCoreApplication::exit(1); return;
                }
                for (const auto& frame : parsed.frames) {
                    const auto json = QJsonDocument::fromJson(frame.payload).object();
                    if (frame.header.type == MessageType::Hello) {
                        const QJsonObject capabilities{{"can_configure", false}, {"can_start_stop", true},
                            {"supports_hardware_timestamps", false}, {"supports_udp", false}};
                        const QJsonObject device{{"device_id", "scripted"}, {"name", "Scripted peer"},
                            {"vendor", "BoundFree"}, {"model", "Test"}, {"firmware", "1"},
                            {"identity_strength", "software"}, {"capabilities", capabilities}};
                        sendJson(MessageType::HelloAck, {{"ok", true}, {"nonce", json.value("nonce")},
                            {"protocol", QJsonObject{{"major", 1}, {"minor", 0}}},
                            {"receive_limits", QJsonObject{{"max_payload", 1048576}, {"max_json", 65536},
                                {"max_channels", 256}, {"max_streams", 16}}},
                            {"features", QJsonArray{"regular_samples.v1"}}, {"session_id", sessionId},
                            {"boot_id", sessionId}, {"device", device}, {"heartbeat_ms", 1000}, {"idle_timeout_ms", 5000}});
                        heartbeat.start();
                    } else if (frame.header.type == MessageType::Command && onCommand) onCommand(json);
                }
            });
        });
    }

    QByteArray frame(MessageType type, const QByteArray& payload, quint32 stream = 0)
    {
        QByteArray bytes;
        if (!bf::bfsp::encode_frame(type, stream, sequences[stream]++, payload, bytes)) {
            qCritical() << "invalid scripted frame" << int(type); QCoreApplication::exit(1);
        }
        return bytes;
    }
    QByteArray jsonFrame(MessageType type, const QJsonObject& json, quint32 stream = 0)
    { return frame(type, QJsonDocument(json).toJson(QJsonDocument::Compact), stream); }
    void sendJson(MessageType type, const QJsonObject& json, quint32 stream = 0)
    { write(jsonFrame(type, json, stream)); }
    void write(const QByteArray& bytes)
    {
        if (peer && peer->state() == QAbstractSocket::ConnectedState) peer->write(bytes);
    }
    QJsonObject response(const QString& id, const QJsonObject& result)
    { return {{"request_id", id}, {"ok", true}, {"result", result}}; }
    void reply(const QString& id, const QJsonObject& result)
    { sendJson(MessageType::Response, response(id, result)); }
    QJsonObject descriptor(quint32 samples, quint32 rateNum, quint32 rateDen = 1)
    {
        return {{"session_id", sessionId}, {"capture_id", captureId}, {"stream_id", 1}, {"descriptor_revision", 1},
            {"name", "Test signal"}, {"signal_type", "voltage"}, {"rate_num", double(rateNum)},
            {"rate_den", double(rateDen)}, {"dtype", "int16"}, {"layout", "channel_major"},
            {"max_samples_per_block", double(samples)}, {"channels", QJsonArray{QJsonObject{
                {"channel_id", "ch1"}, {"name", "CH1"}, {"signal_type", "voltage"}, {"unit", "uV"},
                {"scale", 1}, {"offset", 0}}}}, {"clock", QJsonObject{{"clock_id", 1},
                {"ticks_per_second_num", "1000"}, {"ticks_per_second_den", "1"},
                {"source", "hardware"}, {"epoch", "boot_relative"}}}};
    }
    void sendData()
    {
        QByteArray data(bf::bfsp::kDataPrefixBytes + 2, '\0');
        qToLittleEndian<quint32>(1, data.data());  // descriptor revision
        data[4] = char(bf::bfsp::DType::Int16);
        qToLittleEndian<quint32>(1, data.data() + 8);   // channels
        qToLittleEndian<quint32>(1, data.data() + 12);  // samples
        qToLittleEndian<quint32>(1, data.data() + 32);  // clock
        write(frame(MessageType::Data, data, 1));
    }

    QTcpServer server;
    std::function<void(const QJsonObject&)> onCommand;
private:
    QTcpSocket* peer = nullptr;
    QTimer heartbeat;
    QElapsedTimer clock;
    QHash<quint32, quint32> sequences;
    std::unique_ptr<bf::bfsp::StreamParser> parser;
};

int testScripted(QCoreApplication& app, const QString& testCase)
{
    const bool batch = testCase.startsWith("--parser-batch-");
    const bool reopen = testCase == "--parser-batch-reopen";
    const bool pendingForever = testCase == "--recovery-deadline";
    const bool late = testCase == "--recovery-late-response" || testCase == "--recovery-late-conflict";
    const bool conflict = testCase == "--recovery-late-conflict";
    const bool statusFrame = testCase == "--recovery-status";
    const bool recoveredStart = testCase == "--recovered-start-no-data";
    const bool recovery = testCase.startsWith("--recovery-");
    const bool slow = testCase == "--slow-block";
    const bool longBlock = testCase == "--long-block";
    const bool largePeriod = testCase == "--large-block-period";
    ScriptedPeer peer(app);
    BfspTcpClient client;
    if (!peer.server.listen(QHostAddress::LocalHost)) return 1;
    QElapsedTimer clock;
    clock.start();
    QString prepareId, startId;
    QHash<QString, QString> methods;
    State state = State::Disconnected;
    bool finished = false, passed = false, descriptorSeen = false, prepareSeen = false, accepted = false;
    bool unknownSeen = false, noDataSeen = false, expectedFault = false, endSeen = false;
    int prepares = 0, starts = 0, polls = 0, diagnostics = 0;
    qint64 unknownAt = 0, startedAt = 0;
    const QJsonObject prepared{{"capture_id", captureId}, {"stream_ids", QJsonArray{1}}};
    const QJsonObject started{{"capture_id", captureId}, {"state", "Streaming"}};
    auto finish = [&](bool ok) {
        if (finished) return;
        finished = true; passed = ok;
        if (!ok) qCritical() << testCase << "failed" << int(state) << prepares << starts << polls << diagnostics;
        app.exit(ok ? 0 : 1);
    };
    auto send = [&](const QString& method, const QJsonObject& params = QJsonObject{}) {
        const auto id = requestId();
        methods.insert(id, method);
        client.command(1, id, method, params);
    };
    auto accept = [&] {
        if (accepted || !descriptorSeen) { finish(false); return; }
        accepted = true;
        send("stream.accept", {{"capture_id", captureId}, {"streams", QJsonArray{QJsonObject{
            {"stream_id", 1}, {"descriptor_revision", 1}}}}});
    };
    auto status = [&](bool completed) {
        const auto id = recoveredStart ? startId : prepareId;
        const auto method = recoveredStart ? "capture.start" : "capture.prepare";
        return QJsonObject{{"state", recoveredStart ? "Streaming" : "Prepared"}, {"active_capture_id", captureId},
            {"config_revision", 1}, {"counters", QJsonObject{}}, {"streams", QJsonArray{}},
            {"last_operation", QJsonObject{{"request_id", id}, {"method", method},
                {"state", completed ? "completed" : "pending"},
                {"result", completed ? QJsonValue(recoveredStart ? started : prepared) : QJsonValue(QJsonValue::Null)}}}};
    };

    peer.onCommand = [&](const QJsonObject& command) {
        const auto id = command.value("request_id").toString();
        const auto method = command.value("method").toString();
        if (method == "capture.prepare") {
            ++prepares; prepareId = id;
            if (prepares != 1) { finish(false); return; }  // no mutation replay
            const auto descriptor = peer.descriptor(largePeriod ? 500000 : longBlock ? 64 : slow ? 4 : 1,
                longBlock ? 4 : 1, largePeriod ? 0xffffffffu : 1);
            if (recovery) { peer.sendJson(MessageType::Descriptor, descriptor, 1); return; }
            QByteArray bytes = peer.jsonFrame(MessageType::Response, peer.response(id, prepared));
            bytes += peer.jsonFrame(MessageType::Descriptor, descriptor, 1);
            if (batch) {
                for (int i = 0; i < 8; ++i) {
                    auto bad = peer.jsonFrame(MessageType::Heartbeat, {});
                    bad[bad.size() - 1] = char(static_cast<unsigned char>(bad.back()) ^ 0x80);
                    bytes += bad;
                }
                bf::bfsp::StreamParser check;
                const auto parsed = check.feed(bytes, 0);
                if (!parsed.faulted || parsed.frames.size() != 2 || parsed.errors.size() != 8) {
                    finish(false); return;
                }
            }
            peer.write(bytes);
        } else if (method == "stream.accept") {
            peer.reply(id, {{"accepted", true}});
        } else if (method == "capture.start") {
            ++starts; startId = id; startedAt = clock.elapsed();
            if (starts != 1) { finish(false); return; }
            if (!recoveredStart) {
                peer.reply(id, started);
                QTimer::singleShot(longBlock ? 31000 : 6000, Qt::PreciseTimer, &client, [&] { peer.sendData(); });
            }
        } else if (method == "device.get_status") {
            ++polls;
            if (late && polls == 1) {
                auto result = prepared;
                if (conflict) result.insert("stream_ids", QJsonArray{2});
                peer.reply(prepareId, result);
            }
            peer.reply(id, status(recoveredStart || (!pendingForever && !statusFrame && polls >= 3)));
            if (statusFrame && polls == 1)
                QTimer::singleShot(200, &client, [&] { peer.sendJson(MessageType::Status, status(true)); });
        } else if (method == "capture.stop") {
            const QJsonObject end{{"capture_id", captureId}, {"stream_id", 1}, {"last_sample_exclusive", "0"},
                {"reason", "normal"}, {"complete", true}};
            peer.sendJson(MessageType::StreamEnd, end, 1);
            peer.reply(id, {{"capture_id", captureId}, {"state", "Idle"}, {"streams", QJsonArray{end}}});
        } else finish(false);
    };
    QObject::connect(&client, &BfspTcpClient::stateChanged, &app, [&](quint64 gen, State next) {
        if (gen != 1) return;
        state = next;
        if (next == State::OutcomeUnknown) { unknownSeen = true; unknownAt = clock.elapsed(); }
        if (next == State::Faulted) finish(expectedFault &&
            (batch ? prepareSeen && descriptorSeen && diagnostics == 8 : prepares == 1 && polls >= 3));
    });
    QObject::connect(&client, &BfspTcpClient::connected, &app, [&](quint64 gen, const QJsonObject&) {
        if (gen == 3 && reopen) {
            QTimer::singleShot(200, &client, [&] { finish(prepareSeen && !descriptorSeen && diagnostics == 0); });
        } else if (gen == 1) send("capture.prepare", {{"expected_config_revision", 1}});
        else finish(false);
    });
    QObject::connect(&client, &BfspTcpClient::responseReceived, &app, [&](quint64 gen, const QJsonObject& response) {
        if (gen != 1 || !response.value("ok").toBool()) { finish(false); return; }
        const auto method = methods.take(response.value("request_id").toString());
        if (method == "capture.prepare") {
            if (recovery) { finish(false); return; }  // a late reply cannot outrun reconciliation
            prepareSeen = true;
            if (reopen) { client.close(2); client.open(3, "127.0.0.1", peer.server.serverPort()); return; }
            if (!batch && descriptorSeen) accept();
        } else if (method == "stream.accept") {
            if (recovery) finish(unknownSeen && polls >= (statusFrame ? 1 : 3) && prepares == 1 && state == State::Prepared);
            else send("capture.start", {{"capture_id", captureId}});
        } else if (method == "capture.stop") {
            finish(recoveredStart && unknownSeen && noDataSeen && endSeen && state == State::Ready && starts == 1);
        } else if (method != "capture.start") finish(false);
    });
    QObject::connect(&client, &BfspTcpClient::descriptorReceived, &app, [&](quint64 gen, quint32 stream, const QJsonObject&) {
        if (gen != 1 || stream != 1 || descriptorSeen) { finish(false); return; }
        descriptorSeen = true;
        if (!batch && !recovery && prepareSeen) accept();
    });
    QObject::connect(&client, &BfspTcpClient::statusReceived, &app, [&](quint64 gen, const QJsonObject& observed) {
        if (gen != 1 || !unknownSeen) { finish(false); return; }
        if (observed.value("last_operation").toObject().value("state") != "completed") {
            if (state != State::OutcomeUnknown) finish(false);
            return;
        }
        if (conflict || pendingForever) { finish(false); return; }
        if (recoveredStart) {
            if (state != State::Starting) { finish(false); return; }
            if (noDataSeen) send("capture.stop", {{"capture_id", captureId}});
        } else {
            if (state != State::Prepared || prepareSeen) { finish(false); return; }
            accept();  // synchronous mutation must see the fully restored transport
        }
    });
    QObject::connect(&client, &BfspTcpClient::dataReceived, &app,
        [&](quint64 gen, quint32 stream, const QJsonObject&, const QByteArray& payload) {
            bf::bfsp::DataPrefix prefix;
            if (!bf::bfsp::decode_data(payload, prefix)) { finish(false); return; }
            client.acknowledgeData(gen, captureId, stream, prefix.first_sample_index);
            finish((slow || longBlock || largePeriod) && state == State::Streaming && !noDataSeen &&
                clock.elapsed() - startedAt >= (longBlock ? 30000 : 5000));
        });
    QObject::connect(&client, &BfspTcpClient::streamEnded, &app,
        [&](quint64 gen, quint32 stream, const QJsonObject&) { endSeen = gen == 1 && stream == 1; });
    QObject::connect(&client, &BfspTcpClient::errorOccurred, &app,
        [&](quint64 gen, const QString& code, const QJsonObject& params) {
            if (gen != 1) { finish(false); return; }
            if (code == "request.timeout" && params.value("method") == (recoveredStart ? "capture.start" : "capture.prepare") &&
                params.contains("request_id") && (recovery || recoveredStart)) return;
            if (pendingForever && code == "request.timeout" && params.value("reason") == "recovery_deadline") {
                expectedFault = unknownSeen && clock.elapsed() - unknownAt >= 10000 && clock.elapsed() - unknownAt < 12000;
                return;
            }
            if (conflict && code == "protocol.invalid_payload" && params.value("reason") == "recovery_response_conflict") {
                expectedFault = !prepareSeen && state == State::OutcomeUnknown; return;
            }
            if (batch && code == "bfsp.invalid_frame") {
                ++diagnostics;
                if (!prepareSeen || !descriptorSeen || reopen) finish(false);
                return;
            }
            if (batch && code == "protocol.invalid_frame") { expectedFault = true; return; }
            if (recoveredStart && code == "acquisition.no_data") {
                noDataSeen = true;
                if (clock.elapsed() - unknownAt < 4900) finish(false);
                return;
            }
            finish(false);
        });
    QTimer::singleShot(longBlock ? 38000 : 27000, &client, [&] { finish(false); });
    QTimer::singleShot(0, &client, [&] { client.open(1, "127.0.0.1", peer.server.serverPort()); });
    const int result = app.exec();
    client.close(4);
    if (passed && result == 0) std::printf("BFSP_TCP_CLIENT_TEST PASS %s\n", qPrintable(testCase));
    return passed ? result : 1;
}
}  // namespace

int testIdentity(QCoreApplication& app)
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return 1;
    QElapsedTimer clock;
    clock.start();
    auto parser = std::make_shared<bf::bfsp::StreamParser>();
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        auto* peer = server.nextPendingConnection();
        QObject::connect(peer, &QTcpSocket::readyRead, &app, [&, peer, parser] {
            const auto data = peer->readAll();
            const auto parsed = parser->feed(data, clock.elapsed());
            if (parsed.consumed != data.size() || parsed.faulted) { app.exit(1); return; }
            for (const auto& frame : parsed.frames) {
                QJsonObject hello;
                const auto valid = bf::bfsp::validate_json(frame.header.type, frame.header.stream_id, frame.payload, &hello);
                const bool matches = valid && frame.header.type == bf::bfsp::MessageType::Hello &&
                    hello.value("client_name").toString() == "custom-client" &&
                    hello.value("client_version").toString() == "0.2.2-dev.test";
                app.exit(matches ? 0 : 1);
            }
        });
    });
    QThread thread;
    auto* client = new BfspTcpClient;
    client->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, client, &QObject::deleteLater);
    thread.start();
    const auto port = server.serverPort();
    QMetaObject::invokeMethod(client, [=] {
        client->setClientIdentity("custom-client", "0.2.2-dev.test");
        client->open(1, "127.0.0.1", port);
    }, Qt::QueuedConnection);
    QTimer::singleShot(5000, &app, [&] { app.exit(1); });
    const int result = app.exec();
    QMetaObject::invokeMethod(client, [=] { client->close(2); }, Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
    if (!result) std::puts("BFSP_TCP_CLIENT_TEST PASS identity");
    return result;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--identity")) return testIdentity(app);
    const QStringList scriptedCases{"--recovery-pending", "--recovery-deadline", "--recovery-late-response",
        "--recovery-late-conflict", "--recovery-status", "--parser-batch-fault", "--parser-batch-reopen",
        "--slow-block", "--long-block", "--large-block-period", "--recovered-start-no-data"};
    for (const auto& testCase : scriptedCases)
        if (app.arguments().contains(testCase)) return testScripted(app, testCase);
    const bool loseStart = app.arguments().contains("--lose-start");
    const bool delayStart = app.arguments().contains("--delay-start");
    const bool noData = app.arguments().contains("--no-data");
    const bool earlyStop = app.arguments().contains("--early-stop");
    const bool noAck = app.arguments().contains("--no-ack");
    const bool gap = app.arguments().contains("--gap");
    const bool badCrc = app.arguments().contains("--bad-crc");
    const bool reuseStream = app.arguments().contains("--reuse-stream");
    const bool staleAck = app.arguments().contains("--stale-ack");
    const bool multiple = reuseStream || staleAck || app.arguments().contains("--multiple");
    QString rootPath = qEnvironmentVariable("BF_REPO_ROOT");
#ifdef BF_REPO_ROOT
    if (rootPath.isEmpty()) rootPath = QString::fromUtf8(BF_REPO_ROOT);
#endif
    const auto rootIndex = app.arguments().indexOf("--repo-root");
    if (rootIndex >= 0 && rootIndex + 1 < app.arguments().size()) rootPath = app.arguments().at(rootIndex + 1);
    if (rootPath.isEmpty()) rootPath = QDir::currentPath();
    QDir root(rootPath);
    if (!root.exists("tools/bfsp_simulator/__main__.py")) {
        qCritical() << "set BF_REPO_ROOT or pass --repo-root" << root.absolutePath();
        return 1;
    }
    QProcess simulator;
    simulator.setWorkingDirectory(root.absolutePath());
    QStringList args{"-m", "tools.bfsp_simulator", "--port", "0"};
    if (loseStart) args << "--fault-suppress-start-response";
    if (delayStart || earlyStop) args << "--fault-delay-start-response-ms" << "300";
    if (noData) args << "--fault-suppress-data";
    if (gap) args << "--fault-gap-every" << "3";
    if (badCrc) args << "--fault-corrupt-crc-every" << "3";
    if (reuseStream) args << "--fault-reuse-stream-id";
    QString python = qEnvironmentVariable("BF_PYTHON_EXECUTABLE");
#ifdef Q_OS_WIN
    if (python.isEmpty()) python = QStringLiteral("python");
#else
    if (python.isEmpty()) python = QStringLiteral("python3");
#endif
    simulator.start(python, args);
    if (!simulator.waitForStarted(3000) || !simulator.waitForReadyRead(3000)) {
        qCritical() << "simulator failed to start" << simulator.readAllStandardError();
        return 1;
    }
    const auto output = QString::fromUtf8(simulator.readAllStandardOutput());
    const auto match = QRegularExpression("127\\.0\\.0\\.1', (\\d+)").match(output);
    if (!match.hasMatch()) { qCritical() << "invalid simulator address" << output; return 1; }
    const quint16 port = match.captured(1).toUShort();

    QThread thread;
    auto* client = new BfspTcpClient;
    client->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, client, &QObject::deleteLater);
    thread.start();

    bool passed = false, gotData = false, gotStart = false, gotEnd = false, sawEarlyData = false, sawNoData = false;
    bool sawGap = false, sawCrc = false, sawSequenceGap = false;
    int completedCaptures = 0;
    quint32 currentStream = 0;
    QSet<quint32> observedStreams;
    QString capture, priorCapture;
    QJsonObject config;
    QHash<QString, QString> methods;
    auto send = [&](const QString& method, const QJsonObject& params = {}) {
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
        methods[id] = method;
        QMetaObject::invokeMethod(client, [=] { client->command(1, id, method, params); }, Qt::QueuedConnection);
    };
    auto maybeStop = [&] {
        if (!noAck && !(staleAck && completedCaptures > 0) && gotData && (gotStart || earlyStop) && (!gap || sawGap) &&
            (!badCrc || (sawCrc && sawSequenceGap && sawGap)) && !methods.values().contains("capture.stop"))
            send("capture.stop", {{"capture_id", capture}});
    };

    QObject::connect(client, &BfspTcpClient::connected, &app, [&](quint64 generation, const QJsonObject& ack) {
        if (generation != 1 || !ack.value("ok").toBool()) { app.exit(1); return; }
        config = ack.value("device").toObject().value("capabilities").toObject().value("current_config").toObject();
        if (noData) {
            // Keep the two-block DATA deadline (8.192 s) inside this test's 15 s budget.
            auto groups = config.value("groups").toArray();
            auto group = groups.at(0).toObject();
            group.insert("rate_num", 1000);
            groups[0] = group;
            config.insert("groups", groups);
        }
        send("config.validate", {{"candidate", config}, {"expected_revision", 1}});
    });
    QObject::connect(client, &BfspTcpClient::responseReceived, &app, [&](quint64, const QJsonObject& response) {
        const auto id = response.value("request_id").toString();
        const auto method = methods.take(id);
        if (!response.value("ok").toBool()) { qCritical() << method << response; app.exit(1); return; }
        if (method == "config.validate") send("config.apply", {{"candidate", config}, {"expected_revision", 1}});
        else if (method == "config.apply") send("capture.prepare", {{"expected_config_revision", 2}});
        else if (method == "capture.prepare") capture = response.value("result").toObject().value("capture_id").toString();
        else if (method == "stream.accept") send("capture.start", {{"capture_id", capture}});
        else if (method == "capture.start") { gotStart = true; maybeStop(); }
        else if (method == "capture.stop") {
            passed = (gotData || (noData && sawNoData)) && gotEnd && (!(delayStart || earlyStop) || sawEarlyData);
            ++completedCaptures;
            if (passed && multiple && completedCaptures < 3) {
                gotData = gotStart = gotEnd = sawEarlyData = false;
                priorCapture = capture;
                capture.clear();
                currentStream = 0;
                passed = false;
                send("capture.prepare", {{"expected_config_revision", 2}});
                return;
            }
            if (earlyStop && passed) QTimer::singleShot(500, &app, [&] { app.exit(0); });
            else app.exit(passed ? 0 : 1);
        }
    });
    QObject::connect(client, &BfspTcpClient::descriptorReceived, &app, [&](quint64, quint32 stream, const QJsonObject& descriptor) {
        if (capture.isEmpty() || descriptor.value("capture_id").toString() != capture || observedStreams.contains(stream)) {
            app.exit(1); return;
        }
        observedStreams.insert(stream);
        currentStream = stream;
        send("stream.accept", {{"capture_id", capture}, {"streams", QJsonArray{QJsonObject{
            {"stream_id", double(stream)}, {"descriptor_revision", descriptor.value("descriptor_revision")}}}}});
    });
    QObject::connect(client, &BfspTcpClient::dataReceived, &app,
                     [&](quint64, quint32 stream, const QJsonObject& metadata, const QByteArray& payload) {
        bf::bfsp::DataPrefix prefix;
        double sample = 0;
        if (stream != currentStream || metadata.value("capture_id").toString() != capture ||
            !bf::bfsp::decode_data(payload, prefix) || !bf::bfsp::sample_as_double(payload, prefix, 0, sample) ||
            prefix.channel_count != 2 || prefix.dtype != bf::bfsp::DType::Int24) {
            qCritical() << "invalid DATA" << metadata;
            app.exit(1); return;
        }
        if (!gotStart) sawEarlyData = true;
        gotData = true;
        const auto acknowledgedCapture = staleAck && completedCaptures > 0 ? priorCapture : capture;
        if (!noAck) QMetaObject::invokeMethod(client, [=] {
            client->acknowledgeData(1, acknowledgedCapture, stream, prefix.first_sample_index);
        }, Qt::QueuedConnection);
        maybeStop();
    });
    QObject::connect(client, &BfspTcpClient::streamEnded, &app, [&](quint64, quint32, const QJsonObject& end) {
        gotEnd = end.value("capture_id").toString() == capture && end.value("complete").toBool();
    });
    QObject::connect(client, &BfspTcpClient::statusReceived, &app, [&](quint64, const QJsonObject& status) {
        if (loseStart && status.value("state").toString() == "Streaming" &&
            status.value("active_capture_id").toString() == capture) { gotStart = true; maybeStop(); }
        if (noData && sawNoData && status.value("state").toString() == "Streaming" &&
            status.value("active_capture_id").toString() == capture) send("capture.stop", {{"capture_id", capture}});
    });
    QObject::connect(client, &BfspTcpClient::errorOccurred, &app, [&](quint64, const QString& code, const QJsonObject& params) {
        if (loseStart && code == "request.timeout" && params.value("method") == "capture.start") return;
        if (noData && code == "acquisition.no_data") { sawNoData = true; return; }
        if (noData && code == "acquisition.data_gap") return;
        if ((gap || badCrc) && code == "acquisition.data_gap") { sawGap = true; maybeStop(); return; }
        if (badCrc && code == "bfsp.invalid_frame") { sawCrc = true; maybeStop(); return; }
        if (badCrc && code == "protocol.sequence_gap") { sawSequenceGap = true; maybeStop(); return; }
        if (reuseStream && code == "protocol.invalid_payload" && params.value("reason") == "stream_id_reused" &&
            completedCaptures == 1) { passed = true; app.exit(0); return; }
        if (noAck && code == "capture.backlog_overflow") { passed = gotData; app.exit(passed ? 0 : 1); return; }
        if (staleAck && completedCaptures == 1 && code == "capture.backlog_overflow") {
            passed = gotData; app.exit(passed ? 0 : 1); return;
        }
        qCritical() << "client error" << code << params;
        app.exit(1);
    });
    QTimer::singleShot(15000, &app, [&] { qCritical() << "test deadline exceeded"; app.exit(1); });
    QMetaObject::invokeMethod(client, [=] {
        client->setClientIdentity("BFSP test client", "test-build");
        client->open(1, "127.0.0.1", port, "boundfree.simulator.tcp");
    }, Qt::QueuedConnection);
    const int result = app.exec();
    QMetaObject::invokeMethod(client, [=] { client->close(2); }, Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
    simulator.terminate();
    if (!simulator.waitForFinished(3000)) { simulator.kill(); simulator.waitForFinished(); }
    if (passed) std::puts("BFSP_TCP_CLIENT_TEST PASS");
    else qCritical() << "BFSP TCP lifecycle failed";
    return result;
}
