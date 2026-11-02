#include "controller/capture_recorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QtEndian>

#include <cstdio>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

using namespace bf;
using namespace bf::acquisition;

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("check failed at line ") + std::to_string(__LINE__) + ": " #x); } while (false)

namespace {
bool readExact(char* data, std::size_t size)
{
    while (size) {
        const auto read = std::fread(data, 1, size, stdin);
        if (!read) return false;
        data += read;
        size -= read;
    }
    return true;
}

bool readFrame(quint8& type, QByteArray& payload)
{
    uchar header[5];
    if (!readExact(reinterpret_cast<char*>(header), sizeof(header))) return false;
    type = header[0];
    const auto size = qFromLittleEndian<quint32>(header + 1);
    if (size > 2 * 1024 * 1024) return false;
    payload.resize(size);
    return readExact(payload.data(), size);
}

bool writeResponse(const QJsonObject& response)
{
    const auto payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    uchar header[5]; header[0] = 1;
    qToLittleEndian<quint32>(payload.size(), header + 1);
    return std::fwrite(header, 1, sizeof(header), stdout) == sizeof(header) &&
           std::fwrite(payload.constData(), 1, payload.size(), stdout) == std::size_t(payload.size()) &&
           std::fflush(stdout) == 0;
}

QString fakePath(const QString& name)
{
    return QDir(qEnvironmentVariable("BF_CAPTURE_TEST_DIR")).filePath(name);
}

bool writeJson(const QString& path, const QJsonObject& object = {})
{
    QFile file(path);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QJsonDocument parseTestJson(QByteArray bytes)
{
    // Qt drops a literal initial U+FEFF in JSON strings; the Python sidecar does not.
    bytes.replace(QByteArray("\xef\xbb\xbf", 3), QByteArray("\\uFEFF"));
    return QJsonDocument::fromJson(bytes);
}

// Run as the recorder's subprocess. Per-stream acknowledgements emulate the
// existing stdio envelope, and final summaries deliberately arrive reversed.
int fakeSidecar()
{
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    struct Counts { quint64 accepted = 0, written = 0, flushed = 0; };
    std::map<int, Counts> counts;
    std::map<int, QJsonObject> identities;
    bool sawGap = false;
    const bool overflow = qEnvironmentVariableIsSet("BF_CAPTURE_TEST_OVERFLOW");
    quint8 type = 0;
    QByteArray payload;
    while (readFrame(type, payload)) {
        if (type != 1) return 1;
        const auto request = parseTestJson(payload).object();
        const auto method = request.value("method").toString();
        const auto params = request.value("params").toObject();
        if (method == "capture_append") {
            QByteArray bytes;
            if (!readFrame(type, bytes) || type != 2) return 2;
        }
        if (!writeJson(fakePath(method + ".json"), params)) return 3;
        if (qEnvironmentVariable("BF_CAPTURE_TEST_PAUSE") == method) {
            QElapsedTimer timer; timer.start();
            while (!QFile::exists(fakePath(method + ".release"))) {
                if (timer.elapsed() > 10000) return 6;
                QThread::msleep(1);
            }
        }
        if (qEnvironmentVariable("BF_CAPTURE_TEST_FAIL") == method) {
            if (!writeResponse({{"id", request.value("id")}, {"ok", false},
                                {"error", QJsonObject{{"code", "capture.writer_failed"}, {"params", QJsonObject{{"operation", method}}}}}})) return 5;
            continue;
        }
        QJsonObject result;
        if (method == "hello") {
            result = {{"ipc_protocol_version", 1}, {"product_version", "test"}, {"analysis_capabilities", QJsonArray{}}};
            if (params.contains("requested_capabilities")) {
                const auto requested = params.value("requested_capabilities").toArray();
                QJsonArray advertised;
                if (requested.contains("capture.v1")) advertised.append("capture.v1");
                if (requested.contains("capture.metadata.v1") && !qEnvironmentVariableIsSet("BF_CAPTURE_TEST_LEGACY"))
                    advertised.append("capture.metadata.v1");
                result.insert("capabilities", advertised);
            }
        } else if (method == "capture_open") {
            for (const auto& entry : params.value("streams").toArray()) {
                const auto descriptor = entry.toObject();
                const int id = descriptor.value("stream_id").toInt();
                QJsonObject identity;
                for (const char* key : {"device_id", "boot_id", "session_id", "capture_id", "generation", "stream_id", "descriptor_revision"})
                    identity.insert(QLatin1String(key), descriptor.value(QLatin1String(key)));
                identities.emplace(id, identity);
                counts.emplace(id, Counts{});
            }
            result = {{"handle", "fake-capture"}, {"path", params.value("out_dir").toString()}};
        } else if (method == "capture_append") {
            const auto id = params.value("stream").toObject().value("stream_id").toInt();
            auto& counter = counts.at(id);
            const auto n = static_cast<quint64>(params.value("sample_count").toInt());
            counter.accepted = overflow ? (id == 1 ? std::numeric_limits<quint64>::max() - 3 : 7) : counter.accepted + n;
            counter.written = counter.accepted;
            counter.flushed = overflow ? counter.accepted : counter.written - 1;
            const auto last = params.value("first_sample_index").toString().toULongLong() + n;
            result = {{"accepted", true}, {"accepted_samples", QString::number(counter.accepted)},
                      {"written_samples", QString::number(counter.written)}, {"flushed_samples", QString::number(counter.flushed)},
                      {"last_accepted_sample_exclusive", QString::number(last)}};
        } else if (method == "capture_event") {
            sawGap |= params.value("record").toObject().value("kind").toString() == "data.gap";
            result = {{"accepted", true}};
        } else if (method == "capture_finalize") {
            if (!writeJson(fakePath("finalize.json"), params)) return 3;
            bool complete = !sawGap;
            for (const auto& value : params.value("streams").toArray()) {
                const auto summary = value.toObject();
                complete &= summary.value("complete").toBool() && !summary.value("unknown_gap").toBool() &&
                    summary.value("terminal_sample_exclusive") == summary.value("last_accepted_sample_exclusive");
            }
            QJsonArray summaries;
            for (auto it = counts.rbegin(); it != counts.rend(); ++it) {
                const auto id = it->first;
                const auto& c = it->second;
                summaries.append(QJsonObject{{"stream", identities.at(id)},
                    {"accepted_samples", QString::number(c.accepted)}, {"written_samples", QString::number(c.written)},
                    {"flushed_samples", QString::number(c.written)}});
            }
            const auto corrupt = qEnvironmentVariable("BF_CAPTURE_TEST_CORRUPT");
            if (!corrupt.isEmpty()) {
                auto summary = summaries[0].toObject();
                if (corrupt == "duplicate") summaries[1] = summary;
                else if (corrupt == "missing") summaries.removeLast();
                else if (corrupt == "counter") {
                    summary.insert("accepted_samples", "18446744073709551616");
                    summaries[0] = summary;
                } else {
                    auto identity = summary.value("stream").toObject();
                    const QJsonValue changed = corrupt == "stream_id" || corrupt == "descriptor_revision" ? QJsonValue(99) :
                        QJsonValue(corrupt == "generation" ? "2" : corrupt == "device_id" ? "other-device" : "44444444-4444-4444-8444-444444444444");
                    identity.insert(corrupt, changed);
                    summary.insert("stream", identity);
                    summaries[0] = summary;
                }
            }
            result = {{"status", complete ? "complete" : "partial"}, {"streams", summaries}};
        } else {
            return 4;
        }
        if (!writeResponse({{"id", request.value("id")}, {"ok", true}, {"result", result}})) return 5;
    }
    return 0;
}

bool waitUntil(const std::function<bool()>& condition, int timeoutMs = 10000)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
    return condition();
}

struct Fixture {
    MemoryBudget budget{256 * 1024 * 1024};
    DeviceDescriptor device;
    CaptureDescriptor capture;
    std::shared_ptr<SignalHub> hub;
    std::map<unsigned, quint64> next;

    explicit Fixture(std::uint32_t blockLimit = 16) {
        device.device_id = DeviceId{"test-device"};
        device.boot_id = BootId{"33333333-3333-4333-8333-333333333333"};
        capture.session_id = SessionId{"11111111-1111-4111-8111-111111111111"};
        capture.capture_id = CaptureId{"22222222-2222-4222-8222-222222222222"};
        capture.generation = 1;
        for (unsigned id : {1u, 2u}) {
            StreamDescriptor s;
            s.key = {capture.session_id, id}; s.capture_id = capture.capture_id; s.generation = capture.generation;
            s.revision = 1; s.name = "test"; s.signal_type = "eeg"; s.rate = {250, 1}; s.dtype = DType::Int16;
            s.clock = {id, 1000, 1, ClockSource::Hardware, ClockEpoch::BootRelative, false}; s.max_samples_per_block = blockLimit;
            ChannelDescriptor channel; channel.channel_id = "ch"; channel.name = "ch";
            channel.signal_type = "eeg"; channel.unit = Unit::Microvolt; s.channels.push_back(channel);
            capture.streams.push_back(s);
        }
        hub = std::make_shared<SignalHub>(capture, budget);
    }

    void publish(unsigned id, std::vector<std::int16_t> samples) {
        const auto& s = capture.streams.at(id - 1);
        SampleBlock block;
        block.stream = s.key; block.capture_id = s.capture_id; block.generation = s.generation;
        block.first_sample_index = next[id];
        block.descriptor_revision = s.revision; block.channel_count = 1; block.sample_count = samples.size();
        block.clock_id = s.clock.clock_id; block.host_received_monotonic_ns = 1; block.host_clock_id = "host";
        block.storage = SampleStorage::fromInt16(samples, budget);
        CHECK(block.storage && hub->publish(block).status == PublishStatus::Accepted);
        next[id] += block.sample_count;
    }
};

QJsonObject readSummary(const QString& directory)
{
    QFile file(QDir(directory).filePath("finalize.json"));
    CHECK(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

VersionInfo testVersion()
{
    VersionInfo version; version.ipcProtocolVersion = 1;
    return version;
}

void waitRecording(CaptureRecorder& recorder)
{
    CHECK(waitUntil([&] { return recorder.snapshot().state == CaptureRecordState::Recording || !recorder.isActive(); }));
    CHECK(recorder.snapshot().state == CaptureRecordState::Recording);
}

struct FakeScenario {
    QTemporaryDir directory;

    explicit FakeScenario(const QString& pause = {}, const QString& failure = {}, const QString& corrupt = {}) {
        CHECK(directory.isValid());
        qputenv("BF_CAPTURE_TEST_DIR", directory.path().toUtf8());
        qputenv("BF_CAPTURE_TEST_PAUSE", pause.toUtf8());
        qputenv("BF_CAPTURE_TEST_FAIL", failure.toUtf8());
        qputenv("BF_CAPTURE_TEST_CORRUPT", corrupt.toUtf8());
        qunsetenv("BF_CAPTURE_TEST_OVERFLOW");
        qunsetenv("BF_CAPTURE_TEST_LEGACY");
    }
    ~FakeScenario() {
        for (const char* name : {"BF_CAPTURE_TEST_DIR", "BF_CAPTURE_TEST_PAUSE", "BF_CAPTURE_TEST_FAIL", "BF_CAPTURE_TEST_CORRUPT", "BF_CAPTURE_TEST_OVERFLOW", "BF_CAPTURE_TEST_LEGACY"})
            qunsetenv(name);
    }
    void waitRequest(const QString& method) const {
        CHECK(waitUntil([&] { return QFile::exists(fakePath(method + ".json")); }));
    }
    void release(const QString& method) const { CHECK(writeJson(fakePath(method + ".release"))); }
};

void runCase(const QString& repoRoot, unsigned activeStreams, bool overflow = false)
{
    FakeScenario scenario;
    const auto& directory = scenario.directory;
    if (overflow) qputenv("BF_CAPTURE_TEST_OVERFLOW", "1");
    else qunsetenv("BF_CAPTURE_TEST_OVERFLOW");
    Fixture fixture;
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(directory.path(), fixture.device, fixture.capture, fixture.hub));
    waitRecording(recorder);
    QFile openFile(fakePath("capture_open.json"));
    CHECK(openFile.open(QIODevice::ReadOnly));
    CHECK(!QJsonDocument::fromJson(openFile.readAll()).object().contains("acquisition_metadata"));
    const bool first = activeStreams & 1, second = activeStreams & 2;
    if (first) fixture.publish(1, {1, 2});
    if (second) fixture.publish(2, {3, 4, 5});
    auto expected = overflow ? std::numeric_limits<quint64>::max() : quint64((first ? 2 : 0) + (second ? 3 : 0));
    CHECK(waitUntil([&] { return recorder.snapshot().acceptedSamples == expected ||
                                  recorder.snapshot().state == CaptureRecordState::Failed; }));
    const auto live = recorder.snapshot();
    CHECK(live.state == CaptureRecordState::Recording && live.acceptedSamples == expected && live.writtenSamples == expected);
    CHECK(live.flushedSamples == (overflow ? expected : quint64((first ? 1 : 0) + (second ? 2 : 0))));
    if (first && !overflow) {
        fixture.publish(1, {6});
        ++expected;
        CHECK(waitUntil([&] { return recorder.snapshot().acceptedSamples == expected; }));
        CHECK(recorder.snapshot().writtenSamples == expected && recorder.snapshot().flushedSamples == expected - 1 - unsigned(second));
    }
    recorder.stop();
    CHECK(waitUntil([&] { return !recorder.isActive(); }));
    const auto final = recorder.snapshot();
    CHECK(final.state == (activeStreams == 3 ? CaptureRecordState::Saved : CaptureRecordState::Partial));
    CHECK(final.acceptedSamples == expected && final.writtenSamples == expected && final.flushedSamples == expected);
    const auto summaries = readSummary(directory.path()).value("streams").toArray();
    CHECK(summaries.size() == 2);
    for (const auto& value : summaries) {
        const auto summary = value.toObject();
        const int id = summary.value("stream").toObject().value("stream_id").toInt();
        CHECK((id == 1 || id == 2) && summary.value("complete").toBool() == bool(activeStreams & (1u << (id - 1))));
    }
}

void runOpeningStop(const QString& repoRoot, bool gaps = false, bool interrupted = false)
{
    FakeScenario scenario("capture_open");
    Fixture fixture;
    fixture.capture.metadata = {{"", ""}, {"rate_origin", "requested"}, {"gain_readback", "unknown"},
                                {"boundary", u8"\u8D77\u6B62\u672A\u77E5"}, {u8"\uFEFFrate", "250"},
                                {"bom_value", u8"\uFEFFrequested"}};
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    fixture.capture.metadata["rate_origin"] = "changed after start";
    scenario.waitRequest("capture_open");
    QJsonObject metadata;
    CHECK(waitUntil([&] {
        QFile openFile(fakePath("capture_open.json"));
        if (!openFile.open(QIODevice::ReadOnly)) return false;
        const auto value = parseTestJson(openFile.readAll()).object().value("acquisition_metadata");
        if (!value.isObject()) return false;
        metadata = value.toObject();
        return true;
    }));
    QJsonObject expectedMetadata{{"", ""}, {"rate_origin", "requested"}, {"gain_readback", "unknown"},
                                 {"boundary", QString::fromUtf8(u8"\u8D77\u6B62\u672A\u77E5")},
                                 {"bom_value", QString(QChar(0xfeff)) + "requested"}};
    expectedMetadata.insert(QString(QChar(0xfeff)) + "rate", "250");
    CHECK(metadata == expectedMetadata);
    CHECK(recorder.snapshot().state == CaptureRecordState::Opening);
    fixture.publish(1, {1, 2});
    fixture.publish(2, {3, 4, 5});
    if (gaps) {
        CHECK(!fixture.hub->reportGap({fixture.capture.streams[0].key, 2, 4, "tail"}));
        fixture.next[1] = 6;
        CHECK(!fixture.hub->reportUnknownGap(fixture.capture.streams[1].key, "unknown tail"));
    }
    recorder.stop(interrupted);
    recorder.stop(); // A repeated normal stop must retain interruption state.
    recorder.clearResult();
    CHECK(recorder.isActive() && recorder.snapshot().state == CaptureRecordState::Opening);
    // Publications after stop belong only to acquisition, even before open returns.
    fixture.publish(1, {6});
    fixture.publish(2, {7});
    scenario.release("capture_open");
    CHECK(waitUntil([&] { return !recorder.isActive(); }));
    const auto result = recorder.snapshot();
    CHECK(result.state == (gaps || interrupted ? CaptureRecordState::Partial : CaptureRecordState::Saved));
    CHECK(result.acceptedSamples == 5 && result.writtenSamples == 5 && result.flushedSamples == 5);
    if (gaps) {
        QFile eventFile(fakePath("capture_event.json"));
        CHECK(eventFile.open(QIODevice::ReadOnly));
        const auto event = QJsonDocument::fromJson(eventFile.readAll()).object();
        const auto record = event.value("record").toObject();
        CHECK(event.value("stream").toObject().value("stream_id").toInt() == 2);
        CHECK(record.value("kind") == "data.gap" && record.value("sample_index").isNull() && record.value("count").isNull());
        CHECK(record.value("params").toObject() == (QJsonObject{{"reason", "unknown tail"}, {"detection", "adapter"}}));
    }
    const auto summaries = readSummary(scenario.directory.path()).value("streams").toArray();
    CHECK(summaries.size() == 2);
    for (const auto& entry : summaries) {
        const auto summary = entry.toObject();
        const auto id = summary.value("stream").toObject().value("stream_id").toInt();
        CHECK(id == 1 || id == 2);
        CHECK(summary.value("last_accepted_sample_exclusive").toString() == (id == 1 ? "2" : "3"));
        CHECK(summary.value("complete").toBool() == !interrupted);
        const bool unknown = interrupted || (gaps && id == 2);
        CHECK(summary.value("unknown_gap").toBool() == unknown);
        if (unknown) CHECK(summary.value("terminal_sample_exclusive").isNull());
        else CHECK(summary.value("terminal_sample_exclusive").toString() == (id == 1 ? (gaps ? "6" : "2") : "3"));
    }
}

void runTerminalEventFailure(const QString& repoRoot)
{
    FakeScenario scenario("capture_event", "capture_event");
    Fixture fixture;
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    waitRecording(recorder);
    fixture.publish(1, {1, 2});
    fixture.publish(2, {3, 4, 5});
    CHECK(!fixture.hub->reportUnknownGap(fixture.capture.streams[0].key, "rt_bci.end_boundary_unknown"));
    recorder.stop();
    scenario.waitRequest("capture_event");
    scenario.release("capture_event");
    CHECK(waitUntil([&] { return !recorder.isActive(); }));
    const auto result = recorder.snapshot();
    CHECK(result.state == CaptureRecordState::Failed && result.error.code == "capture.writer_failed");
    CHECK(result.acceptedSamples == 5 && result.writtenSamples == 5);
    CHECK(!QFile::exists(fakePath("finalize.json")));
}

void runDeliveredGap(const QString& repoRoot)
{
    FakeScenario scenario;
    Fixture fixture;
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    waitRecording(recorder);
    CHECK(!fixture.hub->reportUnknownGap(fixture.capture.streams[0].key, "rt_bci.framing_discard"));
    fixture.publish(1, {1, 2});
    fixture.publish(2, {3, 4, 5});
    CHECK(waitUntil([&] { return recorder.snapshot().writtenSamples == 5; }));
    recorder.stop();
    CHECK(waitUntil([&] { return !recorder.isActive(); }));
    CHECK(recorder.snapshot().state == CaptureRecordState::Partial);
    QFile eventFile(fakePath("capture_event.json"));
    CHECK(eventFile.open(QIODevice::ReadOnly));
    const auto event = QJsonDocument::fromJson(eventFile.readAll()).object();
    CHECK(event.value("stream").toObject().value("stream_id").toInt() == 1);
    CHECK(event.value("record").toObject().value("params").toObject() ==
          (QJsonObject{{"reason", "rt_bci.framing_discard"}, {"detection", "adapter"}}));
}

void runInvalidMetadataUtf8(const QString& repoRoot, bool invalidKey)
{
    FakeScenario scenario;
    Fixture fixture;
    const std::string invalid(1, static_cast<char>(0xff));
    fixture.capture.metadata[invalidKey ? invalid : "rate_origin"] = invalidKey ? "requested" : invalid;
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    CHECK(waitUntil([&] { return !recorder.isActive(); }));
    CHECK(recorder.snapshot().state == CaptureRecordState::Failed);
    CHECK(recorder.snapshot().error.code == "protocol.invalid_params");
    CHECK(!QFile::exists(fakePath("capture_open.json")));
}

void runLegacyCapability(const QString& repoRoot, bool withMetadata)
{
    FakeScenario scenario;
    qputenv("BF_CAPTURE_TEST_LEGACY", "1");
    Fixture fixture;
    if (withMetadata) fixture.capture.metadata["rate_origin"] = "requested";
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    if (withMetadata) {
        CHECK(waitUntil([&] { return !recorder.isActive(); }));
        const auto result = recorder.snapshot();
        CHECK(result.state == CaptureRecordState::Failed && result.error.code == "capture.unavailable");
        CHECK(result.error.params.value("dependency") == "capture.metadata.v1");
        CHECK(!QFile::exists(fakePath("capture_open.json")) && !QFile::exists(fakePath("finalize.json")));
    } else {
        waitRecording(recorder);
        fixture.publish(1, {1});
        fixture.publish(2, {2});
        recorder.stop();
        CHECK(waitUntil([&] { return !recorder.isActive(); }));
        CHECK(recorder.snapshot().state == CaptureRecordState::Saved);
    }
    QFile helloFile(fakePath("hello.json"));
    CHECK(helloFile.open(QIODevice::ReadOnly));
    QJsonArray expected{"capture.v1"};
    if (withMetadata) expected.append("capture.metadata.v1");
    CHECK(QJsonDocument::fromJson(helloFile.readAll()).object().value("requested_capabilities").toArray() == expected);
}

void runCancelBeforeOpen(const QString& repoRoot)
{
    FakeScenario scenario("hello");
    Fixture fixture;
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    scenario.waitRequest("hello");
    recorder.stop();
    scenario.release("hello");
    CHECK(waitUntil([&] { return !recorder.isActive(); }));
    CHECK(recorder.snapshot().state == CaptureRecordState::Off);
    CHECK(recorder.snapshot().path.isEmpty() && recorder.snapshot().error.code.isEmpty());
    CHECK(!QFile::exists(fakePath("capture_open.json")) && !QFile::exists(fakePath("finalize.json")));
}

void runFailureStop(const QString& repoRoot, const QString& method)
{
    FakeScenario scenario(method, method);
    Fixture fixture;
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    if (method != "capture_open") {
        waitRecording(recorder);
        fixture.publish(1, {1, 2});
        fixture.publish(2, {3, 4, 5});
    }
    if (method == "capture_finalize") {
        CHECK(waitUntil([&] { return recorder.snapshot().writtenSamples == 5; }));
        recorder.stop();
    }
    scenario.waitRequest(method);
    recorder.stop(true);
    scenario.release(method);
    CHECK(waitUntil([&] { return !recorder.isActive(); }));
    const auto result = recorder.snapshot();
    CHECK(result.state == CaptureRecordState::Failed && result.error.code == "capture.writer_failed");
    CHECK(result.acceptedSamples == quint64(method == "capture_finalize" ? 5 : 0));
    CHECK(!QFile::exists(fakePath("finalize.json")));
    recorder.stop();
    CHECK(recorder.snapshot().state == CaptureRecordState::Failed);
}

void runInvalidSummary(const QString& repoRoot, const QString& corrupt)
{
    FakeScenario scenario({}, {}, corrupt);
    Fixture fixture;
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    waitRecording(recorder);
    fixture.publish(1, {1, 2});
    fixture.publish(2, {3, 4, 5});
    CHECK(waitUntil([&] { return recorder.snapshot().writtenSamples == 5; }));
    recorder.stop();
    CHECK(waitUntil([&] { return !recorder.isActive(); }));
    const auto result = recorder.snapshot();
    CHECK(result.state == CaptureRecordState::Failed && result.error.code == "ipc.invalid_frame");
    CHECK(result.acceptedSamples == 5 && result.writtenSamples == 5 && result.flushedSamples == 3);
}

void runQueueOverflow(const QString& repoRoot)
{
    FakeScenario scenario("capture_append");
    constexpr unsigned samples = 512 * 1024; // 1 MiB per block: the byte limit fails first.
    Fixture fixture(samples);
    CaptureRecorder recorder(repoRoot, testVersion());
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    waitRecording(recorder);
    fixture.publish(1, std::vector<std::int16_t>(samples, 1));
    scenario.waitRequest("capture_append");
    for (int i = 0; i < 65; ++i) fixture.publish(1, std::vector<std::int16_t>(samples, 2));
    recorder.stop();
    scenario.release("capture_append");
    CHECK(waitUntil([&] { return !recorder.isActive(); }, 30000));
    const auto result = recorder.snapshot();
    CHECK(result.state == CaptureRecordState::Failed && result.error.code == "recorder.queue_full");
    CHECK(result.acceptedSamples == quint64(65) * samples && result.writtenSamples == result.acceptedSamples &&
          result.flushedSamples == result.acceptedSamples);
    for (const auto& entry : readSummary(scenario.directory.path()).value("streams").toArray()) {
        const auto summary = entry.toObject();
        CHECK(!summary.value("complete").toBool() && summary.value("unknown_gap").toBool());
        CHECK(summary.value("terminal_sample_exclusive").isNull());
    }
}

void runRestart(const QString& repoRoot, bool failFirst, bool fromStateChanged = false)
{
    FakeScenario scenario({}, failFirst ? "capture_finalize" : "");
    Fixture fixture;
    CaptureRecorder recorder(repoRoot, testVersion());
    int finished = 0;
    bool restarted = false;
    bool terminalSignals = true;
    CaptureRecordSnapshot first;
    const auto restart = [&] {
        qunsetenv("BF_CAPTURE_TEST_FAIL");
        restarted = recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub);
    };
    QObject::connect(&recorder, &CaptureRecorder::finished, &recorder, [&] {
        terminalSignals &= !recorder.isActive();
        if (++finished != 1) return;
        first = recorder.snapshot();
        if (!fromStateChanged) restart();
    });
    QObject::connect(&recorder, &CaptureRecorder::stateChanged, &recorder, [&] {
        if (fromStateChanged && finished == 1 && !recorder.isActive()) restart();
    });
    CHECK(recorder.start(scenario.directory.path(), fixture.device, fixture.capture, fixture.hub));
    waitRecording(recorder);
    fixture.publish(1, {1, 2});
    fixture.publish(2, {3, 4, 5});
    recorder.stop(); // Drain blocks that have not necessarily been acknowledged yet.
    CHECK(waitUntil([&] { return finished >= 1; }));
    CHECK(first.state == (failFirst ? CaptureRecordState::Failed : CaptureRecordState::Saved));
    CHECK(restarted && terminalSignals);
    waitRecording(recorder);
    CHECK(recorder.snapshot().acceptedSamples == 0 && recorder.snapshot().error.code.isEmpty());
    fixture.publish(1, {6});
    fixture.publish(2, {7});
    fixture.hub->close(); // A closed acquisition also drains before finalizing.
    CHECK(waitUntil([&] { return finished == 2; }));
    CHECK(terminalSignals && !recorder.isActive() && recorder.snapshot().state == CaptureRecordState::Saved);
    CHECK(recorder.snapshot().acceptedSamples == 2 && recorder.snapshot().flushedSamples == 2);
}

void runRealCase(const QString& repoRoot, unsigned activeStreams, const QString& tailReason = {})
{
    QTemporaryDir directory;
    CHECK(directory.isValid());
    Fixture fixture;
    fixture.capture.metadata = {{"", ""}, {"rate_origin", "requested"}, {"gain_readback", "unknown"},
                                {"boundary", u8"\u8D77\u6B62\u672A\u77E5"}, {u8"\uFEFFrate", "250"},
                                {"bom_value", u8"\uFEFFrequested"}};
    // Starting mid-capture must preserve the absolute indices and exclude
    // samples published before the recording consumer subscribed.
    fixture.publish(1, {9});
    fixture.publish(2, {8, 9});
    VersionInfo version; version.ipcProtocolVersion = 1;
    CaptureRecorder recorder(repoRoot, version);
    CHECK(recorder.start(directory.path(), fixture.device, fixture.capture, fixture.hub));
    CHECK(waitUntil([&] { return recorder.snapshot().state == CaptureRecordState::Recording ||
                                  recorder.snapshot().state == CaptureRecordState::Failed; }, 30000));
    CHECK(recorder.snapshot().state == CaptureRecordState::Recording);
    const bool first = activeStreams & 1, second = activeStreams & 2;
    if (first) fixture.publish(1, {1, 2});
    if (second) fixture.publish(2, {3, 4, 5});
    const quint64 expected = (first ? 2 : 0) + (second ? 3 : 0);
    CHECK(waitUntil([&] { return recorder.snapshot().writtenSamples == expected ||
                                  recorder.snapshot().state == CaptureRecordState::Failed; }, 30000));
    CHECK(recorder.snapshot().acceptedSamples == expected && recorder.snapshot().writtenSamples == expected);
    if (tailReason.isEmpty()) recorder.stop();
    else {
        CHECK(!fixture.hub->reportUnknownGap(fixture.capture.streams[0].key, tailReason.toStdString()));
        fixture.hub->close(); // The source reports its terminal reason before closing the hub.
    }
    CHECK(waitUntil([&] { return !recorder.isActive(); }, 30000));
    const auto final = recorder.snapshot();
    const bool complete = activeStreams == 3 && tailReason.isEmpty();
    CHECK(final.state == (complete ? CaptureRecordState::Saved : CaptureRecordState::Partial));
    CHECK(final.acceptedSamples == expected && final.writtenSamples == expected && final.flushedSamples == expected);
    QFile manifestFile(QDir(final.path).filePath("manifest.json"));
    CHECK(manifestFile.open(QIODevice::ReadOnly));
    const auto manifest = parseTestJson(manifestFile.readAll()).object();
    CHECK(manifest.value("finalized").toBool());
    QJsonObject expectedMetadata{{"", ""}, {"rate_origin", "requested"}, {"gain_readback", "unknown"},
                                 {"boundary", QString::fromUtf8(u8"\u8D77\u6B62\u672A\u77E5")},
                                 {"bom_value", QString(QChar(0xfeff)) + "requested"}};
    expectedMetadata.insert(QString(QChar(0xfeff)) + "rate", "250");
    CHECK(manifest.value("acquisition_metadata").toObject() == expectedMetadata);
    CHECK(manifest.value("status").toString() == (complete ? "complete" : "partial"));
    const auto streams = manifest.value("streams").toArray();
    CHECK(streams.size() == 2);
    for (const auto& entry : streams) {
        const auto value = entry.toObject();
        const int id = value.value("descriptor").toObject().value("stream_id").toInt();
        const auto summary = value.value("summary").toObject();
        CHECK(id == 1 || id == 2);
        CHECK(summary.value("complete").toBool() == bool(activeStreams & (1u << (id - 1))));
        CHECK(summary.value("unknown_gap").toBool() == (!tailReason.isEmpty() && id == 1));
        CHECK(summary.value("written_samples").toString() == (id == 1 ? (first ? "2" : "0") : (second ? "3" : "0")));
    }
    QProcess python;
#ifdef Q_OS_WIN
    const auto executable = qEnvironmentVariable("BF_PYTHON_EXECUTABLE", "python");
#else
    const auto executable = qEnvironmentVariable("BF_PYTHON_EXECUTABLE", "python3");
#endif
    const QString script = QStringLiteral(R"PY(
import json, pathlib, sys
import h5py
p = pathlib.Path(sys.argv[1]); active = int(sys.argv[2]); tail_reason = sys.argv[3]
manifest = json.loads((p / 'manifest.json').read_text(encoding='utf-8'))
assert manifest['acquisition_metadata'][''] == ''
assert manifest['acquisition_metadata']['\ufeffrate'] == '250'
assert manifest['acquisition_metadata']['bom_value'] == '\ufeffrequested'
assert {s['descriptor']['stream_id'] for s in manifest['streams']} == {1, 2}
events = [json.loads(line) for line in (p / 'events.jsonl').read_text(encoding='utf-8').splitlines()]
if tail_reason:
    recorded = [event for event in events if event['params']['reason'] == tail_reason]
    assert len(recorded) == 1
    event = recorded[0]
    desc = next(item['descriptor'] for item in manifest['streams'] if item['descriptor']['stream_id'] == 1)
    identity = ('device_id', 'boot_id', 'session_id', 'capture_id', 'generation', 'stream_id', 'descriptor_revision')
    assert event['stream'] == {key: desc[key] for key in identity}
    assert event['kind'] == 'data.gap' and event['params']['detection'] == 'adapter'
    assert event['sample_index'] is None and event['count'] is None
    assert any(e['params']['reason'] == 'unknown_end_or_gap' and e['params']['detection'] == 'final_summary' for e in events)
else:
    assert not events
with h5py.File(p / 'signals.h5', 'r') as file:
    for item in manifest['streams']:
        desc = item['descriptor']; stream = desc['stream_id']
        group = file[f"streams/{desc['session_id']}/{stream}"]
        data = group['samples'][:]; blocks = group['blocks'][:]
        expected = ([1, 2] if stream == 1 else [3, 4, 5]) if active & (1 << (stream - 1)) else []
        assert data.dtype.kind == 'i' and data.dtype.itemsize == 2
        assert data.shape == (len(expected), 1) and data[:, 0].tolist() == expected
        assert desc['record_start_sample_index'] == str(stream)
        assert len(blocks) == bool(expected)
        # The adapter event and final summary are two records, not two lost samples.
        gaps = group['gaps'][:]
        assert len(gaps) == (2 if tail_reason and stream == 1 else 0)
        assert all(int(gap['count_known']) == 0 for gap in gaps)
        if expected:
            assert int(blocks[0]['sample_index']) == stream and int(blocks[0]['sample_count']) == len(expected)
            assert int(blocks[0]['host_received_monotonic_ns']) == 1
            assert int(blocks[0]['descriptor_revision']) == desc['descriptor_revision']
        summary = item['summary']
        assert all(summary[k] == str(len(expected)) for k in ('accepted_samples', 'written_samples', 'flushed_samples'))
        assert summary['last_accepted_sample_exclusive'] == str(stream + len(expected))
)PY");
    python.start(executable, {"-c", script, final.path, QString::number(activeStreams), tailReason});
    CHECK(python.waitForFinished(10000));
    if (python.exitStatus() != QProcess::NormalExit || python.exitCode() != 0)
        std::cerr << python.readAllStandardError().constData();
    CHECK(python.exitStatus() == QProcess::NormalExit && python.exitCode() == 0);
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--stdio")) return fakeSidecar();
    try {
        CHECK(app.arguments().size() >= 2);
        if (app.arguments().contains("--real-sidecar")) {
            qunsetenv("BF_SIDECAR_EXECUTABLE");
        } else {
            qputenv("BF_SIDECAR_EXECUTABLE", QCoreApplication::applicationFilePath().toUtf8());
            for (unsigned active : {3u, 1u, 2u, 0u}) runCase(app.arguments()[1], active);
            runCase(app.arguments()[1], 3, true);
            runOpeningStop(app.arguments()[1]);
            runOpeningStop(app.arguments()[1], true);
            runOpeningStop(app.arguments()[1], false, true);
            runTerminalEventFailure(app.arguments()[1]);
            runDeliveredGap(app.arguments()[1]);
            runInvalidMetadataUtf8(app.arguments()[1], true);
            runInvalidMetadataUtf8(app.arguments()[1], false);
            runLegacyCapability(app.arguments()[1], true);
            runLegacyCapability(app.arguments()[1], false);
            runCancelBeforeOpen(app.arguments()[1]);
            for (const char* method : {"capture_open", "capture_append", "capture_finalize"}) runFailureStop(app.arguments()[1], method);
            for (const char* corrupt : {"device_id", "boot_id", "session_id", "capture_id", "generation", "stream_id", "descriptor_revision",
                                        "duplicate", "missing", "counter"}) runInvalidSummary(app.arguments()[1], corrupt);
            runQueueOverflow(app.arguments()[1]);
            runRestart(app.arguments()[1], false);
            runRestart(app.arguments()[1], true);
            runRestart(app.arguments()[1], false, true);
            qunsetenv("BF_SIDECAR_EXECUTABLE");
        }
        for (unsigned active : {3u, 1u, 2u, 0u}) runRealCase(app.arguments()[1], active);
        runRealCase(app.arguments()[1], 3, "rt_bci.end_boundary_unknown");
        runRealCase(app.arguments()[1], 3, "rt_bci.unconfirmed_tail_groups=1");
        std::cout << "capture_recorder_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "capture_recorder_test: " << error.what() << '\n';
        return 1;
    }
}
