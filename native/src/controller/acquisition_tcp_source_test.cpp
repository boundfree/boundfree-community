#include "controller/acquisition_tcp_source.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>

#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <set>
#include <utility>

using namespace bf;
using namespace bf::acquisition;

namespace {

// The TCP receive profile is 16 streams; the protocol permits up to 64.
constexpr int ReceiveProfileMaxStreams = 16;

bool waitUntil(const std::function<bool()>& predicate, int timeout = 10000)
{
    QElapsedTimer timer;
    timer.start();
    bool ready = predicate();
    while (!ready && timer.elapsed() < timeout) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
        ready = predicate();
    }
    QCoreApplication::processEvents();
    return ready;
}

QString pythonExecutable()
{
    auto result = qEnvironmentVariable("BF_PYTHON_EXECUTABLE");
#ifdef Q_OS_WIN
    if (result.isEmpty()) result = QStringLiteral("python");
#else
    if (result.isEmpty()) result = QStringLiteral("python3");
#endif
    return result;
}

class Simulator {
public:
    bool start(const QString& root, const QStringList& extra = {})
    {
        process.setWorkingDirectory(root);
        process.start(pythonExecutable(), QStringList{"-m", "tools.bfsp_simulator", "--port", "0"} + extra);
        return readPort();
    }
    bool startWithTerminalGap(const QString& root, const QString& mode = QStringLiteral("ok"))
    {
        // Use the real simulator with a controlled terminal EVENT and a
        // fractional sample rate to test the complete recording boundary.
        const char* script = R"PY(
import asyncio, copy, struct, sys, uuid
from tools.bfsp_simulator import server, wire
mode = sys.argv[1] if len(sys.argv) > 1 else 'ok'
multi_modes = ('multi', 'multi-partial-cancel', 'multi-partial-timeout', 'multi-mixed-simulated', 'profile-streams')
old_init = server.Session.__init__
def init(self, *args):
    old_init(self, *args)
    self.config['groups'][0]['rate_num'] = 250
    self.config['groups'][0]['rate_den'] = 3
server.Session.__init__ = init
if mode in multi_modes:
    old_produce = server.Session._produce
    async def produce_multiple(self):
        self.extra_indices = {d['stream_id']: 0 for d in self.extra_descriptors}
        async def extras():
            while True:
                await asyncio.sleep(0.04)
                for descriptor in self.extra_descriptors:
                    stream = descriptor['stream_id']
                    index = self.extra_indices[stream]
                    self.extra_indices[stream] += 1
                    ticks = self.start_ticks + index * 1000000000 // 25
                    payload = wire.DATA_PREFIX.pack(self.config_revision, 4, 0, 1, 1, 1,
                                                    index, ticks, descriptor['clock']['clock_id'], 0, 0) + struct.pack('<f', 22 + index / 100)
                    self._send(wire.DATA, stream, payload)
        task = asyncio.create_task(extras())
        try:
            await old_produce(self)
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
    server.Session._produce = produce_multiple
if len(sys.argv) > 1 and sys.argv[1] == 'readonly':
    old_device = server.Session._device
    def readonly_device(self):
        device = old_device(self)
        device['capabilities']['can_configure'] = False
        return device
    server.Session._device = readonly_device
if mode in ('no-cap-simulated', 'descriptor-simulation-switch', 'multi-mixed-simulated',
            'multi-partial-cancel', 'multi-partial-timeout', 'real-descriptors'):
    old_device = server.Session._device
    def no_simulated_capability(self):
        device = old_device(self)
        device['capabilities'].pop('simulated', None)
        return device
    server.Session._device = no_simulated_capability
if len(sys.argv) > 1 and sys.argv[1] == 'lost-apply':
    old_send = server.Session._send_json
    def drop_apply_response(self, kind, stream, obj):
        result = obj.get('result', {})
        if (kind == wire.RESPONSE and 'effective_config' in result and 'config_revision' in result
                and not getattr(self, '_dropped_apply', False)):
            self._dropped_apply = True
            return
        old_send(self, kind, stream, obj)
    server.Session._send_json = drop_apply_response
old_execute = server.Session._execute
async def execute(self, method, params):
    if (mode.startswith('config-fail-') and method == 'config.' + mode.split('-')[2]):
        code = 'device.config_partial' if mode.endswith('-partial') else 'device.config_conflict'
        raise server.CommandError(code, {'reason': 'test_config_uncertain', 'method': method,
                                        'config_revision': self.config_revision}, True)
    if method == 'device.get_status' and getattr(self, '_failed_terminal_once', False):
        if mode == 'stop-fail-status-error':
            raise server.CommandError('device.busy', {'reason': 'status_unavailable'}, True)
        result, followups = await old_execute(self, method, params)
        if mode == 'stop-fail-wrong-capture':
            result['active_capture_id'] = str(uuid.uuid4())
        elif mode == 'stop-fail-unknown-operation':
            result['last_operation'] = dict(result['last_operation'], state='unknown')
        elif mode == 'stop-fail-wrong-method':
            result['last_operation'] = dict(result['last_operation'], method='capture.discard')
        elif mode == 'stop-fail-stopping':
            result['state'] = 'Stopping'
        return result, followups
    if mode in ('missing-descriptor', 'descriptor-timeout', 'discard-fail-missing') and method == 'capture.prepare':
        result, followups = await old_execute(self, method, params)
        if not getattr(self, '_omitted_descriptor', False):
            self._omitted_descriptor = True
            followups = []
        return result, followups
    if ((mode.startswith('discard-fail') and method == 'capture.discard') or
            (mode.startswith('stop-fail') and method == 'capture.stop')):
        if not getattr(self, '_failed_terminal_once', False):
            self._failed_terminal_once = True
            raise server.CommandError('device.busy', {'reason': 'test_transient'}, True)
    if mode in multi_modes:
        if method == 'capture.prepare':
            result, followups = await old_execute(self, method, params)
            self.extra_descriptors = []
            for index in range(int(sys.argv[2]) - 1 if mode == 'profile-streams' else 1):
                stream = self.next_stream_id
                self.next_stream_id += 1
                other = copy.deepcopy(self.descriptor)
                other.update(stream_id=stream, name='Temperature', signal_type='temperature',
                             rate_num=25, rate_den=1, dtype='float32')
                other['clock']['clock_id'] = index + 2
                other['channels'] = [{'channel_id': 't1', 'name': 'TEMP', 'signal_type': 'temperature',
                                      'unit': 'degC', 'scale': 1, 'offset': 0}]
                result['stream_ids'].append(stream)
                followups.append((wire.DESCRIPTOR, stream, other))
                self.extra_descriptors.append(other)
            if mode == 'multi-mixed-simulated':
                self.descriptor['clock']['simulated'] = False
            if mode in ('multi-partial-cancel', 'multi-partial-timeout') and not getattr(self, '_omitted_descriptor', False):
                self._omitted_descriptor = True
                followups = followups[:1]
            return result, followups
        if method == 'stream.accept':
            self._require_capture(params, {'Prepared'})
            expected = [{'stream_id': stream, 'descriptor_revision': self.config_revision}
                        for stream in [self.stream_id] + [d['stream_id'] for d in self.extra_descriptors]]
            if params.get('streams') != expected:
                raise server.CommandError('protocol.invalid_payload')
            self.accepted = True
            return {'accepted': True}, []
        if method == 'capture.stop' and self.state == 'Streaming' and params.get('capture_id') == self.capture_id:
            self.state = 'Stopping'
            self.producer_task.cancel()
            await asyncio.gather(self.producer_task, return_exceptions=True)
            self.producer_task = None
            await self._flush()
            ends = [{'capture_id': self.capture_id, 'stream_id': stream, 'last_sample_exclusive': str(last),
                     'reason': 'normal', 'complete': True} for stream, last in
                    [(self.stream_id, self.sample_index)] + list(self.extra_indices.items())]
            for end in ends:
                self._send_json(wire.STREAM_END, end['stream_id'], end)
            capture = self.capture_id
            self.state, self.capture_id, self.stream_id, self.descriptor, self.accepted = 'Idle', None, None, None, False
            return {'state': 'Idle', 'capture_id': capture, 'streams': ends}, []
    if mode in ('cap-only-simulated', 'real-descriptors', 'descriptor-simulation-switch') and method == 'capture.prepare':
        result, followups = await old_execute(self, method, params)
        self.descriptor['clock']['simulated'] = mode == 'descriptor-simulation-switch' and result['stream_ids'][0] == 1
        return result, followups
    if method == 'capture.stop' and self.state == 'Streaming' and params.get('capture_id') == self.capture_id:
        if self.producer_task:
            self.producer_task.cancel()
            await asyncio.gather(self.producer_task, return_exceptions=True)
            self.producer_task = None
        await self._flush()
        first = self.sample_index
        self.sample_index += 4
        self._send_json(wire.EVENT, self.stream_id, {
            'event_id': str(uuid.uuid4()), 'kind': 'data.gap', 'stream_id': self.stream_id,
            'descriptor_revision': self.config_revision + (1 if len(sys.argv) > 1 and sys.argv[1] == 'bad' else 0),
            'sample_index': str(first), 'count': '4',
            'params': {'reason': 'test_tail', 'detection': 'sample_counter'}})
    return await old_execute(self, method, params)
server.Session._execute = execute
async def main():
    simulator = server.SimulatorServer()
    listener = await simulator.start('127.0.0.1', 0)
    print('BFSP simulator listening on ' + str(listener.sockets[0].getsockname()), flush=True)
    async with listener:
        await listener.serve_forever()
asyncio.run(main())
)PY";
        process.setWorkingDirectory(root);
        process.start(pythonExecutable(), {"-c", QString::fromUtf8(script), mode, QString::number(ReceiveProfileMaxStreams)});
        return readPort();
    }
    bool readPort()
    {
        if (!process.waitForStarted(5000)) return false;
        QElapsedTimer deadline; deadline.start();
        QByteArray output;
        while (deadline.elapsed() < 5000 && process.state() != QProcess::NotRunning) {
            process.waitForReadyRead(100);
            output += process.readAllStandardOutput();
            const auto match = QRegularExpression("127\\.0\\.0\\.1', (\\d+)").match(QString::fromUtf8(output));
            if (match.hasMatch()) { port = match.captured(1).toUShort(); return port != 0; }
        }
        qWarning() << "Simulator startup:" << output << process.readAllStandardError();
        return false;
    }
    ~Simulator()
    {
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            if (!process.waitForFinished(3000)) { process.kill(); process.waitForFinished(); }
        }
    }
    quint16 port = 0;
    QProcess process;
};

int fail(const char* message, const AcquisitionTcpSource& source)
{
    const auto view = source.snapshot();
    qCritical("FAIL %s (connection=%d acquisition=%d samples=%llu gaps=%llu unknown=%llu error=%s)", message,
              int(view.connection), int(view.acquisition), static_cast<unsigned long long>(view.samples),
              static_cast<unsigned long long>(view.missingSamples), static_cast<unsigned long long>(view.unknownGaps),
              qPrintable(view.error.code));
    return 1;
}

bool isConnected(const AcquisitionTcpSource& source)
{ return source.snapshot().connection == DeviceConnectionState::Connected; }
bool isIdle(const AcquisitionTcpSource& source)
{ return source.snapshot().acquisition == DeviceAcquisitionState::Idle; }

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir logDirectory; qputenv("BF_ACQUISITION_LOG_DIRECTORY", logDirectory.path().toUtf8());
    QString root = qEnvironmentVariable("BF_REPO_ROOT");
#ifdef BF_REPO_ROOT
    if (root.isEmpty()) root = QString::fromUtf8(BF_REPO_ROOT);
#endif
    if (argc > 1) root = QString::fromLocal8Bit(argv[1]);
    if (root.isEmpty()) root = QDir::currentPath();
    if (!QDir(root).exists("tools/bfsp_simulator/__main__.py")) {
        qCritical("set BF_REPO_ROOT or supply the repository root as an argument");
        return 1;
    }
    Simulator server;
    if (!server.start(root)) { qCritical("failed to start BFSP simulator"); return 1; }
    VersionInfo version;
    QString versionError;
    if (!VersionService(root).load(&version, &versionError)) { qCritical() << versionError; return 1; }
    AcquisitionTcpSource source(version);
    source.open("127.0.0.1", server.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(source); })) return fail("connect", source);
    if (!source.snapshot().simulated || !source.snapshot().canConfigure) return fail("declared simulator capability", source);
    const auto diagnosticPath = source.snapshot().diagnosticLogPath;
    if (diagnosticPath.isEmpty() || !QFile::exists(diagnosticPath)) return fail("TCP session log created", source);
    const auto initialConfigCompletion = source.snapshot().configCompletion;
    source.configure(500);
    if (!waitUntil([&] { const auto view = source.snapshot(); return view.sampleRate == 500 &&
                view.configCompletion == initialConfigCompletion + 1 && !view.configurationInProgress && view.configRevision == 2; }))
        return fail("configure", source);

    std::set<std::string> captures;
    for (std::uint32_t expectedStream = 1; expectedStream <= 3; ++expectedStream) {
        source.startAcquisition();
        if (!waitUntil([&] {
            const auto view = source.snapshot();
            return view.acquisition == DeviceAcquisitionState::Streaming && view.samples >= 100;
        })) return fail("start and sample", source);
        const auto context = source.captureContext();
        if (!context || context->capture.streams.size() != 1 || !context->hub ||
            context->device.device_id.value != "boundfree.simulator.tcp" ||
            context->capture.streams[0].key.stream_id != expectedStream ||
            context->capture.streams[0].dtype != DType::Int24 ||
            !context->capture.streams[0].clock.simulated ||
            source.snapshot().exactRate.numerator != 500 || source.snapshot().exactRate.denominator != 1 ||
            !captures.insert(context->capture.capture_id.value).second) return fail("capture identity", source);
        auto recording = context->hub->addConsumer(ConsumerKind::Recording, {2 * 1024 * 1024, 64});
        if (!recording) return fail("subscribe", source);
        const auto consumer = *recording.value;
        const auto starts = consumer->stats().start_sample_index;
        const auto streamKey = context->capture.streams[0].key;
        const auto start = starts.find(streamKey);
        if (start == starts.end() || start->second == 0) return fail("inherited sample index", source);
        std::optional<Delivery> delivery;
        if (!waitUntil([&] { delivery = consumer->pop(); return delivery.has_value(); })) return fail("recording block", source);
        const auto& block = *delivery->block;
        const auto* raw = block.storage->data<std::int32_t>();
        if (!raw || block.storage->dtype() != DType::Int24 || block.first_sample_index != start->second ||
            block.capture_id != context->capture.capture_id || block.generation != context->capture.generation ||
            block.stream != streamKey || raw->empty()) return fail("raw int24 and capture binding", source);
        const double expected = std::sin(2 * std::acos(-1.0) * (10.0 * block.first_sample_index / 500.0)) * 1000.0;
        if (std::abs(double(raw->front()) - std::nearbyint(expected)) > 1) return fail("raw ADC value", source);
        source.stopAcquisition();
        if (!waitUntil([&] { return isIdle(source) && !source.captureContext(); })) return fail("stop", source);
        const auto stopped = source.snapshot();
        if (stopped.samples == 0 || stopped.traces.empty() || consumer->stats().state != ConsumerState::Stopped ||
            context->hub->publish(block).status != PublishStatus::Stale) return fail("closed hub and retained trace", source);
    }

    // Queue cancel immediately after start. A subsequent capture must use id 5:
    // id 4 was prepared then discarded, never falsely reported as a live run.
    QCoreApplication::processEvents();
    bool observed = false;
    const auto observer = QObject::connect(&source, &AcquisitionTcpSource::stateChanged, &app, [&] { observed = true; });
    source.startAcquisition();
    source.stopAcquisition();
    if (!waitUntil([&] { return observed && isIdle(source); })) return fail("cancel prepare", source);
    QObject::disconnect(observer);
    source.startAcquisition();
    if (!waitUntil([&] { return source.snapshot().acquisition == DeviceAcquisitionState::Streaming; })) return fail("start after cancel", source);
    const auto afterCancel = source.captureContext();
    if (!afterCancel || afterCancel->capture.streams[0].key.stream_id != 5) return fail("discard verified", source);
    source.stopAcquisition();
    if (!waitUntil([&] { return isIdle(source) && !source.captureContext(); })) return fail("stop after cancel", source);
    source.close();
    if (!waitUntil([&] { return source.snapshot().connection == DeviceConnectionState::Disconnected; })) return fail("close", source);

    Simulator interruptedServer;
    if (!interruptedServer.start(root, {"--fault-disconnect-after", "30"})) return fail("start fault server", source);
    AcquisitionTcpSource interrupted(version);
    interrupted.open("127.0.0.1", interruptedServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(interrupted); })) return fail("connect fault server", interrupted);
    interrupted.startAcquisition();
    if (!waitUntil([&] { return interrupted.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
        return fail("fault run start", interrupted);
    const auto interruptedContext = interrupted.captureContext();
    if (!interruptedContext) return fail("fault run context", interrupted);
    auto recorder = interruptedContext->hub->addConsumer(ConsumerKind::Recording, {2 * 1024 * 1024, 64});
    if (!recorder) return fail("fault recording subscription", interrupted);
    if (!waitUntil([&] { return interrupted.snapshot().connection == DeviceConnectionState::Faulted; }))
        return fail("disconnect detection", interrupted);
    if (interrupted.snapshot().unknownGaps == 0 || interruptedContext->hub->stats().unknown_gaps == 0 ||
        (*recorder.value)->stats().state != ConsumerState::Stopped || interrupted.captureContext())
        return fail("disconnect unknown gap and hub close", interrupted);

    Simulator terminalServer;
    if (!terminalServer.startWithTerminalGap(root)) return fail("start terminal gap server", source);
    AcquisitionTcpSource terminal(version);
    terminal.open("127.0.0.1", terminalServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(terminal); })) return fail("terminal connect", terminal);
    if (terminal.snapshot().exactRate.numerator != 250 || terminal.snapshot().exactRate.denominator != 3 ||
        terminal.snapshot().sampleRate != 0 || !terminal.snapshot().simulated) return fail("fractional rate", terminal);
    terminal.startAcquisition();
    if (!waitUntil([&] { return terminal.snapshot().acquisition == DeviceAcquisitionState::Streaming &&
                                 terminal.snapshot().samples >= 20; })) return fail("terminal capture", terminal);
    const auto terminalContext = terminal.captureContext();
    if (!terminalContext || terminalContext->capture.streams[0].rate.numerator != 250 ||
        terminalContext->capture.streams[0].rate.denominator != 3 || !terminalContext->capture.streams[0].clock.simulated)
        return fail("terminal descriptor", terminal);
    QTemporaryDir directory;
    if (!directory.isValid()) return fail("temporary recording directory", terminal);
    CaptureRecorder recording(root, version);
    if (!recording.start(directory.path(), terminalContext->device, terminalContext->capture, terminalContext->hub))
        return fail("late recorder start", terminal);
    if (!waitUntil([&] { return recording.snapshot().state == CaptureRecordState::Recording &&
                                 recording.snapshot().writtenSamples >= 10; }, 30000)) return fail("late recorder write", terminal);
    terminal.stopAcquisition();
    if (!waitUntil([&] { return isIdle(terminal) && !terminal.captureContext(); })) return fail("terminal stop", terminal);
    if (!waitUntil([&] { return !recording.isActive(); }, 30000)) return fail("terminal recorder finalize", terminal);
    if (recording.snapshot().state != CaptureRecordState::Partial || terminal.snapshot().missingSamples != 4)
        return fail("terminal gap classification", terminal);
    QFile manifestFile(QDir(recording.snapshot().path).filePath("manifest.json"));
    if (!manifestFile.open(QIODevice::ReadOnly)) return fail("recorded manifest", terminal);
    const auto manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const auto streams = manifest.value("streams").toArray();
    if (streams.size() != 1) return fail("recorded streams", terminal);
    const auto saved = streams.first().toObject();
    const auto descriptor = saved.value("descriptor").toObject();
    const auto summary = saved.value("summary").toObject();
    bool validStart = false, validLast = false, validEnd = false;
    const auto start = descriptor.value("record_start_sample_index").toString().toULongLong(&validStart);
    const auto last = summary.value("last_accepted_sample_exclusive").toString().toULongLong(&validLast);
    const auto end = summary.value("terminal_sample_exclusive").toString().toULongLong(&validEnd);
    if (!validStart || !validLast || !validEnd || start == 0 || last <= start || end != last + 4 ||
        !descriptor.value("clock").toObject().value("simulated").toBool() ||
        descriptor.value("dtype").toString() != "int24" || descriptor.value("rate_num").toInt() != 250 ||
        descriptor.value("rate_den").toInt() != 3 || summary.value("gap_records").toString() != "1")
        return fail("late recorder identity, simulation and terminal gap", terminal);

    Simulator invalidEventServer;
    if (!invalidEventServer.startWithTerminalGap(root, "bad")) return fail("start invalid event server", source);
    AcquisitionTcpSource invalidEvent(version);
    invalidEvent.open("127.0.0.1", invalidEventServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(invalidEvent); })) return fail("invalid event connect", invalidEvent);
    invalidEvent.startAcquisition();
    if (!waitUntil([&] { return invalidEvent.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
        return fail("invalid event capture", invalidEvent);
    invalidEvent.stopAcquisition();
    if (!waitUntil([&] { return invalidEvent.snapshot().connection == DeviceConnectionState::Faulted; }))
        return fail("invalid event revision rejected", invalidEvent);
    if (invalidEvent.snapshot().error.code != "bfsp.invalid_payload" || invalidEvent.snapshot().unknownGaps == 0)
        return fail("invalid event preserves unknown gap", invalidEvent);

    Simulator readonlyServer;
    if (!readonlyServer.startWithTerminalGap(root, "readonly")) return fail("start readonly server", source);
    AcquisitionTcpSource readonly(version);
    readonly.open("127.0.0.1", readonlyServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(readonly); }) || readonly.snapshot().canConfigure)
        return fail("readonly capability", readonly);
    readonly.configure(250);
    if (!waitUntil([&] { return readonly.snapshot().error.code == "capability.unsupported"; }))
        return fail("readonly configuration rejected", readonly);
    readonly.startAcquisition();
    if (!waitUntil([&] { return readonly.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
        return fail("readonly capture", readonly);
    readonly.stopAcquisition();
    if (!waitUntil([&] { return isIdle(readonly) && !readonly.captureContext(); })) return fail("readonly stop", readonly);

    Simulator noDataServer;
    if (!noDataServer.start(root, {"--fault-suppress-data"})) return fail("start no-data server", source);
    AcquisitionTcpSource noData(version);
    noData.open("127.0.0.1", noDataServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(noData); })) return fail("no-data connect", noData);
    // The peer declares up to 4096 samples per block. At 1000 Hz the two-block
    // watchdog is 8.192 s, keeping this regression within its 10 s wait budget.
    noData.configure(1000);
    if (!waitUntil([&] { return noData.snapshot().configRevision == 2 && noData.snapshot().canConfigure; }))
        return fail("no-data configuration", noData);
    noData.startAcquisition();
    if (!waitUntil([&] { return noData.snapshot().error.code == "acquisition.no_data"; }, 10000) ||
        noData.snapshot().acquisition != DeviceAcquisitionState::Starting) return fail("no-data status", noData);
    noData.stopAcquisition();
    if (!waitUntil([&] { return isIdle(noData) && !noData.captureContext(); }, 10000) ||
        noData.snapshot().missingSamples == 0) return fail("no-data status reconciliation and stop", noData);

    Simulator lostApplyServer;
    if (!lostApplyServer.startWithTerminalGap(root, "lost-apply")) return fail("start lost-apply server", source);
    AcquisitionTcpSource lostApply(version);
    lostApply.open("127.0.0.1", lostApplyServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(lostApply); })) return fail("lost-apply connect", lostApply);
    const auto beforeLostApply = lostApply.snapshot().configCompletion;
    lostApply.configure(500);
    if (!waitUntil([&] { const auto view = lostApply.snapshot(); return view.sampleRate == 500 && view.canConfigure &&
                !view.configurationInProgress && view.configCompletion == beforeLostApply + 1 && view.configRevision == 2; }, 15000))
        return fail("configuration timeout reconciliation", lostApply);
    lostApply.startAcquisition();
    if (!waitUntil([&] { return lostApply.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
        return fail("start after recovered configuration", lostApply);
    lostApply.stopAcquisition();
    if (!waitUntil([&] { return isIdle(lostApply) && !lostApply.captureContext(); }))
        return fail("stop after recovered configuration", lostApply);

    for (const auto& method : QStringList{"validate", "apply"}) {
        for (const auto& error : QStringList{"partial", "conflict"}) {
            const auto mode = QStringLiteral("config-fail-%1-%2").arg(method, error);
            const auto code = QStringLiteral("device.config_%1").arg(error);
            Simulator configServer;
            if (!configServer.startWithTerminalGap(root, mode)) return fail("start configuration failure server", source);
            AcquisitionTcpSource failedConfig(version);
            failedConfig.open("127.0.0.1", configServer.port, "boundfree.simulator.tcp");
            if (!waitUntil([&] { return isConnected(failedConfig); })) return fail("configuration failure connect", failedConfig);
            const auto beforeConfig = failedConfig.snapshot();
            failedConfig.configure(500);
            if (!waitUntil([&] { return failedConfig.snapshot().connection == DeviceConnectionState::Faulted; }, 5000))
                return fail("uncertain configuration faults directly", failedConfig);
            const auto faulted = failedConfig.snapshot();
            const QJsonObject expectedParams{{"reason", "test_config_uncertain"}, {"method", "config." + method},
                                             {"config_revision", double(beforeConfig.configRevision)}};
            if (faulted.acquisition != DeviceAcquisitionState::Faulted || faulted.error.code != code ||
                faulted.error.params != expectedParams || faulted.configurationInProgress || faulted.canConfigure ||
                faulted.configCompletion != beforeConfig.configCompletion + 1 || failedConfig.captureContext())
                return fail("configuration cause and one completion retained", failedConfig);
            failedConfig.startAcquisition();
            if (!waitUntil([&] { return failedConfig.snapshot().acquisitionCompletion > faulted.acquisitionCompletion; }))
                return fail("start rejected after uncertain configuration", failedConfig);
            const auto rejected = failedConfig.snapshot();
            if (rejected.connection != DeviceConnectionState::Faulted || rejected.acquisition != DeviceAcquisitionState::Faulted ||
                rejected.canConfigure || rejected.configCompletion != beforeConfig.configCompletion + 1 || failedConfig.captureContext())
                return fail("faulted configuration cannot start", failedConfig);
            failedConfig.open("127.0.0.1", configServer.port, "boundfree.simulator.tcp");
            if (!waitUntil([&] { const auto view = failedConfig.snapshot(); return isConnected(failedConfig) &&
                        view.acquisition == DeviceAcquisitionState::Idle && view.canConfigure && !view.configurationInProgress &&
                        view.error.code.isEmpty(); }))
                return fail("reconnect refreshes configuration after fault", failedConfig);
            failedConfig.startAcquisition();
            if (!waitUntil([&] { return failedConfig.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
                return fail("start after configuration fault reconnect", failedConfig);
            failedConfig.stopAcquisition();
            if (!waitUntil([&] { return isIdle(failedConfig) && !failedConfig.captureContext(); }))
                return fail("stop after configuration fault reconnect", failedConfig);
        }
    }

    Simulator multiServer;
    if (!multiServer.startWithTerminalGap(root, "multi")) return fail("start multi-stream server", source);
    AcquisitionTcpSource multi(version);
    multi.open("127.0.0.1", multiServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(multi); })) return fail("multi-stream connect", multi);
    multi.startAcquisition();
    if (!waitUntil([&] {
        const auto view = multi.snapshot();
        return view.acquisition == DeviceAcquisitionState::Streaming && view.totalChannels == 3 &&
               view.traces.size() == 3 && view.traces[2].points.size() >= 2;
    })) return fail("multi-stream traces", multi);
    const auto multiView = multi.snapshot();
    const auto multiContext = multi.captureContext();
    if (!multiContext || multiContext->capture.streams.size() != 2 ||
        !multiView.traces[2].name.endsWith(" / TEMP") || multiView.traces[2].unit != QString::fromUtf8("°C") ||
        std::abs(multiView.traces[2].points[1].seconds - multiView.traces[2].points[0].seconds - 0.04) > 1e-6)
        return fail("multi-stream rate and unit", multi);
    multi.stopAcquisition();
    if (!waitUntil([&] { return isIdle(multi) && !multi.captureContext(); })) return fail("multi-stream stop", multi);

    {
        Simulator manyServer;
        if (!manyServer.startWithTerminalGap(root, "profile-streams")) return fail("start maximum stream server", source);
        AcquisitionTcpSource many(version);
        many.open("127.0.0.1", manyServer.port, "boundfree.simulator.tcp");
        if (!waitUntil([&] { return isConnected(many); })) return fail("maximum stream connect", many);
        many.startAcquisition();
        if (!waitUntil([&] { return many.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
            return fail("maximum stream prepare and start", many);
        const auto context = many.captureContext();
        const auto view = many.snapshot();
        if (!context || context->capture.streams.size() != ReceiveProfileMaxStreams || view.totalChannels != ReceiveProfileMaxStreams + 1 ||
            view.exactRate.numerator != 250 || view.exactRate.denominator != 3)
            return fail("receive profile independent of display", many);
        auto recording = context->hub->addConsumer(ConsumerKind::Recording, {2 * 1024 * 1024, 1024});
        if (!recording) return fail("maximum stream recording subscription", many);
        std::set<std::uint32_t> recordedStreams;
        if (!waitUntil([&] {
            while (const auto delivery = (*recording.value)->pop()) recordedStreams.insert(delivery->block->stream.stream_id);
            return recordedStreams.size() == ReceiveProfileMaxStreams;
        })) return fail("all receive profile streams reach recording consumer", many);
        const auto recordedView = many.snapshot();
        if (recordedView.traces.empty() || recordedView.traces.size() >= recordedView.totalChannels ||
            (*recording.value)->stats().state != ConsumerState::Active)
            return fail("display reduction keeps recording streams", many);
        many.stopAcquisition();
        if (!waitUntil([&] { return isIdle(many) && !many.captureContext(); }) ||
            (*recording.value)->stats().state != ConsumerState::Stopped)
            return fail("maximum stream stop", many);
    }

    Simulator missingServer;
    if (!missingServer.startWithTerminalGap(root, "missing-descriptor")) return fail("start missing descriptor server", source);
    AcquisitionTcpSource missing(version);
    missing.open("127.0.0.1", missingServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(missing); })) return fail("missing descriptor connect", missing);
    missing.startAcquisition();
    if (!waitUntil([&] { return missing.snapshot().acquisition == DeviceAcquisitionState::Starting; }))
        return fail("missing descriptor prepare", missing);
    QElapsedTimer settle;
    settle.start();
    waitUntil([&] { return settle.elapsed() >= 250; }, 1000);
    const auto beforeCancel = missing.snapshot().acquisitionCompletion;
    missing.stopAcquisition();
    if (!waitUntil([&] { return isIdle(missing) && missing.snapshot().acquisitionCompletion > beforeCancel; }, 3000))
        return fail("cancel before descriptor", missing);
    missing.startAcquisition();
    if (!waitUntil([&] { return missing.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
        return fail("restart after missing descriptor cancel", missing);
    const auto missingContext = missing.captureContext();
    if (!missingContext || missingContext->capture.streams.front().key.stream_id != 2)
        return fail("discard acknowledged before restart", missing);
    missing.stopAcquisition();
    if (!waitUntil([&] { return isIdle(missing) && !missing.captureContext(); })) return fail("missing descriptor stop", missing);

    Simulator deadlineServer;
    if (!deadlineServer.startWithTerminalGap(root, "descriptor-timeout")) return fail("start descriptor deadline server", source);
    AcquisitionTcpSource deadline(version);
    deadline.open("127.0.0.1", deadlineServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(deadline); })) return fail("descriptor deadline connect", deadline);
    const auto beforeDeadline = deadline.snapshot().acquisitionCompletion;
    deadline.startAcquisition();
    if (!waitUntil([&] { const auto view = deadline.snapshot(); return view.acquisition == DeviceAcquisitionState::Idle &&
                view.acquisitionCompletion > beforeDeadline && view.error.code == "request.timeout" &&
                view.error.params.value("method") == "capture.descriptor"; }, 8000))
        return fail("descriptor deadline discards capture", deadline);
    deadline.startAcquisition();
    if (!waitUntil([&] { return deadline.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
        return fail("restart after descriptor deadline", deadline);
    deadline.stopAcquisition();
    if (!waitUntil([&] { return isIdle(deadline) && !deadline.captureContext(); })) return fail("descriptor deadline stop", deadline);

    // Receiving one descriptor must not make a missing second descriptor wait
    // forever, nor prevent cancellation once prepare has supplied capture_id.
    for (const auto& mode : QStringList{"multi-partial-cancel", "multi-partial-timeout"}) {
        Simulator partialServer;
        if (!partialServer.startWithTerminalGap(root, mode)) return fail("start partial descriptor server", source);
        AcquisitionTcpSource partial(version);
        partial.open("127.0.0.1", partialServer.port, "boundfree.simulator.tcp");
        if (!waitUntil([&] { return isConnected(partial); }) || partial.snapshot().simulated)
            return fail("partial descriptor connect", partial);
        const auto beforePartial = partial.snapshot();
        partial.startAcquisition();
        // This capability is absent; true proves the first descriptor arrived.
        if (!waitUntil([&] { const auto view = partial.snapshot(); return view.simulated &&
                    view.acquisition == DeviceAcquisitionState::Starting; }, 3000) || partial.captureContext())
            return fail("partial descriptor retained before accept", partial);
        const bool cancel = mode == "multi-partial-cancel";
        if (cancel) partial.stopAcquisition();
        if (!waitUntil([&] { const auto view = partial.snapshot(); return isConnected(partial) &&
                    view.acquisition == DeviceAcquisitionState::Idle && view.acquisitionCompletion == beforePartial.acquisitionCompletion + 1;
                }, cancel ? 3000 : 8000))
            return fail("partial descriptor bounded cleanup", partial);
        const auto cleaned = partial.snapshot();
        if (partial.captureContext() || cleaned.configCompletion != beforePartial.configCompletion ||
            !cleaned.canConfigure || (!cancel && (cleaned.error.code != "request.timeout" ||
                cleaned.error.params.value("method") != "capture.descriptor")))
            return fail("partial descriptor cleanup result", partial);
        partial.startAcquisition();
        if (!waitUntil([&] { return partial.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
            return fail("restart after partial descriptor cleanup", partial);
        const auto context = partial.captureContext();
        if (!context || context->capture.streams.size() != 2 || context->capture.streams.front().key.stream_id != 3 ||
            partial.snapshot().exactRate.numerator != 250 || partial.snapshot().exactRate.denominator != 3)
            return fail("partial discard preserves multi-stream identity and rate", partial);
        partial.stopAcquisition();
        if (!waitUntil([&] { return isIdle(partial) && !partial.captureContext(); })) return fail("partial descriptor stop", partial);
    }

    Simulator stopFailServer;
    if (!stopFailServer.startWithTerminalGap(root, "stop-fail")) return fail("start stop failure server", source);
    AcquisitionTcpSource stopFail(version);
    stopFail.open("127.0.0.1", stopFailServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(stopFail); })) return fail("stop failure connect", stopFail);
    stopFail.startAcquisition();
    if (!waitUntil([&] { return stopFail.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
        return fail("stop failure capture", stopFail);
    const auto stopFailContext = stopFail.captureContext();
    if (!stopFailContext) return fail("stop failure context", stopFail);
    auto failureConsumer = stopFailContext->hub->addConsumer(ConsumerKind::Recording, {2 * 1024 * 1024, 64});
    if (!failureConsumer) return fail("stop failure consumer", stopFail);
    const auto beforeStopFailure = stopFail.snapshot().acquisitionCompletion;
    const auto stopConfigCompletion = stopFail.snapshot().configCompletion;
    stopFail.stopAcquisition();
    if (!waitUntil([&] { const auto view = stopFail.snapshot(); return view.acquisition == DeviceAcquisitionState::Streaming &&
                view.error.code == "device.busy" && view.acquisitionCompletion == beforeStopFailure + 1; }))
        return fail("failed stop status restores streaming", stopFail);
    const auto restoredContext = stopFail.captureContext();
    if (!isConnected(stopFail) || !restoredContext || restoredContext->hub != stopFailContext->hub ||
        restoredContext->capture.capture_id != stopFailContext->capture.capture_id ||
        (*failureConsumer.value)->stats().state != ConsumerState::Active ||
        stopFail.snapshot().configCompletion != stopConfigCompletion || !stopFail.snapshot().error.retryable)
        return fail("failed stop preserves live capture and consumer", stopFail);
    stopFail.stopAcquisition();
    if (!waitUntil([&] { return isIdle(stopFail) && !stopFail.captureContext() &&
                stopFail.snapshot().acquisitionCompletion == beforeStopFailure + 2; }) ||
        (*failureConsumer.value)->stats().state != ConsumerState::Stopped)
        return fail("retry failed stop", stopFail);

    // An incompatible observed state or an unconfirmed failed operation must
    // finish visibly instead of leaving an unresolvable Stopping snapshot.
    const std::pair<const char*, const char*> terminalFaults[] = {
        {"stop-fail-wrong-capture", "bfsp.invalid_payload"},
        {"stop-fail-unknown-operation", "device.invalid_state"},
        {"stop-fail-wrong-method", "device.invalid_state"},
        {"stop-fail-stopping", "device.invalid_state"},
        {"stop-fail-status-error", "request.timeout"},
    };
    for (const auto& test : terminalFaults) {
        Simulator faultServer;
        if (!faultServer.startWithTerminalGap(root, QString::fromLatin1(test.first))) return fail("start terminal fault server", source);
        AcquisitionTcpSource failed(version);
        failed.open("127.0.0.1", faultServer.port, "boundfree.simulator.tcp");
        if (!waitUntil([&] { return isConnected(failed); })) return fail(test.first, failed);
        failed.startAcquisition();
        if (!waitUntil([&] { return failed.snapshot().acquisition == DeviceAcquisitionState::Streaming; })) return fail(test.first, failed);
        const auto context = failed.captureContext();
        const auto beforeFailure = failed.snapshot();
        failed.stopAcquisition();
        if (!waitUntil([&] { return failed.snapshot().connection == DeviceConnectionState::Faulted; }, 5000)) return fail(test.first, failed);
        const auto view = failed.snapshot();
        if (!context || failed.captureContext() || view.acquisition != DeviceAcquisitionState::Faulted ||
            view.error.code != QString::fromLatin1(test.second) || view.acquisitionCompletion != beforeFailure.acquisitionCompletion + 1 ||
            view.configCompletion != beforeFailure.configCompletion || context->hub->stats().unknown_gaps == 0)
            return fail(test.first, failed);
    }

    for (const auto& mode : QStringList{"discard-fail", "discard-fail-missing"}) {
        Simulator discardFailServer;
        if (!discardFailServer.startWithTerminalGap(root, mode)) return fail("start discard failure server", source);
        AcquisitionTcpSource discardFail(version);
        discardFail.open("127.0.0.1", discardFailServer.port, "boundfree.simulator.tcp");
        if (!waitUntil([&] { return isConnected(discardFail); })) return fail("discard failure connect", discardFail);
        const auto beforeDiscardFailure = discardFail.snapshot();
        discardFail.startAcquisition();
        discardFail.stopAcquisition();
        if (!waitUntil([&] { const auto view = discardFail.snapshot(); return view.acquisition == DeviceAcquisitionState::Starting &&
                    view.error.code == "device.busy" && view.acquisitionCompletion == beforeDiscardFailure.acquisitionCompletion + 1; }))
            return fail("failed discard status restores prepared", discardFail);
        if (!isConnected(discardFail) || discardFail.captureContext() || !discardFail.snapshot().error.retryable ||
            discardFail.snapshot().configCompletion != beforeDiscardFailure.configCompletion)
            return fail("failed discard retains prepared capture without accepting", discardFail);
        discardFail.stopAcquisition();
        if (!waitUntil([&] { return isIdle(discardFail) && !discardFail.captureContext() &&
                    discardFail.snapshot().acquisitionCompletion == beforeDiscardFailure.acquisitionCompletion + 2; }))
            return fail("retry failed discard", discardFail);
        discardFail.startAcquisition();
        if (!waitUntil([&] { return discardFail.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
            return fail("restart after failed discard retry", discardFail);
        const auto context = discardFail.captureContext();
        if (!context || context->capture.streams.front().key.stream_id != 2)
            return fail("failed discard retry keeps original capture identity", discardFail);
        discardFail.stopAcquisition();
        if (!waitUntil([&] { return isIdle(discardFail) && !discardFail.captureContext(); })) return fail("stop after discard retry", discardFail);
    }

    Simulator implicitSimServer;
    if (!implicitSimServer.startWithTerminalGap(root, "descriptor-simulation-switch")) return fail("start descriptor simulation server", source);
    AcquisitionTcpSource implicitSim(version);
    implicitSim.open("127.0.0.1", implicitSimServer.port, "boundfree.simulator.tcp");
    if (!waitUntil([&] { return isConnected(implicitSim); }) || implicitSim.snapshot().simulated)
        return fail("unspecified simulation capability", implicitSim);
    implicitSim.startAcquisition();
    if (!waitUntil([&] { const auto view = implicitSim.snapshot(); return view.acquisition == DeviceAcquisitionState::Streaming && view.simulated; }))
        return fail("descriptor simulation identity", implicitSim);
    implicitSim.stopAcquisition();
    if (!waitUntil([&] { return isIdle(implicitSim) && !implicitSim.captureContext(); })) return fail("descriptor simulation stop", implicitSim);
    implicitSim.startAcquisition();
    if (!waitUntil([&] { return implicitSim.snapshot().acquisition == DeviceAcquisitionState::Streaming; }) || implicitSim.snapshot().simulated)
        return fail("descriptor simulation reset for next capture", implicitSim);
    implicitSim.stopAcquisition();
    if (!waitUntil([&] { return isIdle(implicitSim) && !implicitSim.captureContext(); })) return fail("second descriptor simulation stop", implicitSim);

    for (const auto& mode : QStringList{"cap-only-simulated", "real-descriptors", "multi-mixed-simulated"}) {
        Simulator simulationServer;
        if (!simulationServer.startWithTerminalGap(root, mode)) return fail("start simulation identity server", source);
        AcquisitionTcpSource simulation(version);
        simulation.open("127.0.0.1", simulationServer.port, "boundfree.simulator.tcp");
        if (!waitUntil([&] { return isConnected(simulation); })) return fail("simulation identity connect", simulation);
        simulation.startAcquisition();
        if (!waitUntil([&] { return simulation.snapshot().acquisition == DeviceAcquisitionState::Streaming; }))
            return fail("simulation identity capture", simulation);
        const auto context = simulation.captureContext();
        const bool mixed = mode == "multi-mixed-simulated";
        if (!context || simulation.snapshot().simulated != (mode != "real-descriptors") ||
            context->capture.streams.front().clock.simulated ||
            (mixed && (context->capture.streams.size() != 2 || !context->capture.streams.back().clock.simulated)))
            return fail("capability OR any descriptor simulation", simulation);
        simulation.stopAcquisition();
        if (!waitUntil([&] { return isIdle(simulation) && !simulation.captureContext(); })) return fail("simulation identity stop", simulation);
    }
    QFile diagnostic(diagnosticPath);
    if (!diagnostic.open(QIODevice::ReadOnly)) return fail("TCP session log retained", source);
    const auto logBytes = diagnostic.readAll();
    if (!logBytes.contains("signal_stats") || !logBytes.contains("bfsp.tcp") || !logBytes.contains("\"simulated\":true"))
        return fail("TCP common schema/provenance/statistics", source);
    std::puts("ACQUISITION_TCP_SOURCE_TEST PASS");
    return 0;
}
