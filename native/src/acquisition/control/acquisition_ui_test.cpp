#include "acquisition/control/acquisition_controller.h"
#include "acquisition/sources/acquisition_tcp_source.h"
#include "acquisition/control/acquisition_view.h"
#include "core/version.h"
#include "i18n/i18n_service.h"
#include "ipc/sidecar_client.h"
#include "ui/device_workspace.h"
#include "ui/waveform_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QProcess>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QToolButton>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>
#include <memory>

using namespace bf;

// A second stream must not be interpreted using the displayed stream's
// channel layout. Missing samples must break the line without inserting zero.
static bool testViewProjection() {
    using namespace acquisition;
    MemoryBudget budget(1024 * 1024);
    StreamDescriptor first;
    first.key = {SessionId{"session"}, 1}; first.capture_id = CaptureId{"capture"}; first.generation = 1;
    first.name = "voltage"; first.signal_type = "voltage"; first.rate = {100, 1}; first.dtype = DType::Int16;
    first.clock = {1, 1000000000, 1, ClockSource::Estimated, ClockEpoch::SessionRelative, false};
    first.max_samples_per_block = 2;
    ChannelDescriptor voltage; voltage.channel_id = "v"; voltage.name = "Voltage";
    voltage.signal_type = "voltage"; voltage.unit = Unit::Millivolt; voltage.scale = 0.5; voltage.offset = 1;
    ChannelDescriptor temperature; temperature.channel_id = "t"; temperature.name = "Temperature";
    temperature.signal_type = "temperature"; temperature.unit = Unit::Celsius;
    first.channels = {voltage, temperature};
    auto second = first; second.key.stream_id = 2; second.name = "thermal"; second.rate = {25, 2};
    second.clock.clock_id = 2; // Display does not require or establish a shared clock.
    second.channels.clear();
    for (int i = 0; i < 18; ++i) {
        auto channel = temperature;
        channel.channel_id = "t" + std::to_string(i);
        channel.name = "Sensor" + std::to_string(i + 1);
        channel.scale = 0.25; channel.offset = 10;
        second.channels.push_back(channel);
    }
    CaptureDescriptor capture{first.key.session_id, first.capture_id, 1, {first, second}};
    SignalHub hub(capture, budget);
    auto added = hub.addConsumer(ConsumerKind::History, {65536, 32});
    if (!added) return false;
    const auto publish = [&](const StreamDescriptor& d, quint64 index, const std::vector<std::int16_t>& values) {
        SampleBlock block; block.stream = d.key; block.capture_id = d.capture_id; block.generation = d.generation;
        block.descriptor_revision = d.revision; block.channel_count = d.channels.size(); block.sample_count = 2;
        block.first_sample_index = index; block.clock_id = d.clock.clock_id;
        block.storage = SampleStorage::fromInt16(values, budget);
        return hub.publish(block).status == PublishStatus::Accepted;
    };
    std::vector<std::int16_t> thermalFirst, thermalSecond;
    for (int i = 0; i < 18; ++i) {
        thermalFirst.push_back(40 + i); thermalFirst.push_back(80 + i);
        thermalSecond.push_back(48 + i); thermalSecond.push_back(52 + i);
    }
    if (!publish(first, 0, {0, 10, 5, 6}) || !publish(second, 0, thermalFirst) ||
        !publish(first, 3, {4, 6, 7, 8}) || !publish(second, 3, thermalSecond)) return false;
    const auto traces = buildAcquisitionTraces(first, *added.value);
    if (!(traces.size() == 2 && traces[0].unit == "mV" && traces[1].unit == QString::fromUtf8("°C") &&
          traces[0].points.size() == 4 && traces[0].points[2].gapBefore &&
          std::abs(traces[0].points[0].value - 1.0) < 1e-12 &&
          std::abs(traces[0].points[2].value - 3.0) < 1e-12 &&
          std::abs(traces[0].points[2].seconds - 0.03) < 1e-12)) return false;
    const auto combined = buildAcquisitionTraces(capture, *added.value);
    if (!(combined.size() == 16 && combined[0].name == "voltage [1] / Voltage" &&
          combined[1].name == "voltage [1] / Temperature" && combined[2].name == "thermal [2] / Sensor1" &&
          combined[15].name == "thermal [2] / Sensor14" && combined[0].unit == "mV" &&
          combined[0].multipleStreams && combined[2].multipleStreams &&
          combined[0].rate.numerator == 100 && combined[0].rate.denominator == 1 &&
          combined[2].rate.numerator == 25 && combined[2].rate.denominator == 2 &&
          combined[2].unit == QString::fromUtf8("°C") && combined[0].points[2].gapBefore &&
          combined[2].points.size() == 4 && combined[2].points[2].gapBefore &&
          std::abs(combined[2].points[0].value - 20.0) < 1e-12 &&
          std::abs(combined[2].points[2].value - 22.0) < 1e-12 &&
          std::abs(combined[2].points[2].seconds - 0.24) < 1e-12)) return false;
    const CaptureDescriptor singleCapture{first.key.session_id, first.capture_id, 1, {first}};
    const auto single = buildAcquisitionTraces(singleCapture, *added.value);
    if (single.size() != 2 || single[0].name != "Voltage" || single[0].multipleStreams) return false;
    const auto range = acquisitionTraceTimeRange(combined);
    if (!range || std::abs(range->first) > 1e-12 || std::abs(range->last - 0.32) > 1e-12 ||
        std::abs((combined[0].points.back().seconds - range->first) / (range->last - range->first) - 0.125) > 1e-12 ||
        acquisitionTraceTimeRange({})) return false;

    MemoryBudget fragmentedBudget(16 * 1024 * 1024);
    auto fragmentedDescriptor = first;
    fragmentedDescriptor.rate = {1000, 1}; fragmentedDescriptor.max_samples_per_block = 1;
    fragmentedDescriptor.channels = {voltage}; fragmentedDescriptor.channels[0].scale = 1;
    fragmentedDescriptor.channels[0].offset = 0;
    CaptureDescriptor fragmentedCapture{first.key.session_id, first.capture_id, 1, {fragmentedDescriptor}};
    SignalHub fragmentedHub(fragmentedCapture, fragmentedBudget);
    auto fragmentedHistory = fragmentedHub.addConsumer(ConsumerKind::History, {8 * 1024 * 1024, 8192});
    if (!fragmentedHistory) return false;
    for (quint64 i = 0; i < 8192; ++i) {
        if (i == 4000) continue; // A missing sample inside a display bucket.
        const std::int16_t value = i == 1000 ? 32000 : i == 6000 ? -32000 : 0;
        SampleBlock block; block.stream = fragmentedDescriptor.key; block.capture_id = first.capture_id;
        block.generation = 1; block.descriptor_revision = first.revision; block.channel_count = 1;
        block.sample_count = 1; block.first_sample_index = i; block.clock_id = first.clock.clock_id;
        block.storage = SampleStorage::fromInt16({value}, fragmentedBudget);
        if (!block.storage || fragmentedHub.publish(block).status != PublishStatus::Accepted) return false;
    }
    const auto reduced = buildAcquisitionTraces(fragmentedDescriptor, *fragmentedHistory.value);
    if (reduced.size() != 1 || reduced[0].points.size() > 1600 || reduced[0].points.empty() ||
        std::abs(reduced[0].points.front().seconds) > 1e-12 ||
        std::abs(reduced[0].points.back().seconds - 8.191) > 1e-12) return false;
    bool positiveSpike = false, negativeSpike = false, gapBroken = false;
    for (std::size_t i = 0; i < reduced[0].points.size(); ++i) {
        const auto& point = reduced[0].points[i];
        positiveSpike |= point.value == 32000; negativeSpike |= point.value == -32000;
        if (i && reduced[0].points[i - 1].seconds < 4.0 && point.seconds > 4.0)
            gapBroken = point.gapBefore;
    }
    return positiveSpike && negativeSpike && gapBroken;
}

static bool testTraceTimeRange() {
    AcquisitionTrace fast, slow, single, empty;
    fast.rate = {1000, 1}; fast.unit = "mV";
    fast.points = {{12.0, 1, false}, {12.1, 2, false}};
    slow.rate = {25, 2}; slow.unit = QString::fromUtf8("°C");
    slow.points = {{11.92, 20, false}, {12.16, 22, true}};
    single.points = {{12.24, 0, false}};
    const auto range = acquisitionTraceTimeRange({empty, fast, slow, single});
    const auto reversed = acquisitionTraceTimeRange({single, slow, fast, empty});
    if (!range || !reversed || std::abs(range->first - 11.92) > 1e-12 ||
        std::abs(range->last - 12.24) > 1e-12 || range->first != reversed->first ||
        range->last != reversed->last) return false;
    // A short trace keeps its real position in the shared domain; it must not
    // stretch to the full plot width. The endpoints come from different streams.
    const auto position = [&](double seconds) { return (seconds - range->first) / (range->last - range->first); };
    if (std::abs(position(fast.points.front().seconds) - 0.25) > 1e-12 ||
        std::abs(position(fast.points.back().seconds) - 0.5625) > 1e-12) return false;
    const auto singleton = acquisitionTraceTimeRange({single, empty});
    if (!singleton || singleton->first != 12.24 || singleton->last != 12.24 ||
        acquisitionTraceTimeRange({empty})) return false;
    AcquisitionTrace invalid;
    invalid.points = {{std::numeric_limits<double>::quiet_NaN(), 0, false}};
    if (acquisitionTraceTimeRange({invalid})) return false;
    invalid.points = {{1, 0, false}, {std::numeric_limits<double>::infinity(), 0, false}};
    if (acquisitionTraceTimeRange({invalid})) return false;
    invalid.points = {{2, 0, false}, {1, 0, false}};
    const auto validOnly = acquisitionTraceTimeRange({invalid, fast});
    return validOnly && validOnly->first == 12.0 && validOnly->last == 12.1;
}

static bool waitUntil(const std::function<bool()>& predicate, int ms = 10000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < ms) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    QApplication::processEvents();
    return predicate();
}
// The source uses its own event loop. Holding the GUI event queue until the
// worker reaches a terminal state deterministically exercises coalescing.
static bool waitWithoutGuiEvents(const std::function<bool()>& predicate, int ms = 10000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < ms) QThread::yieldCurrentThread();
    return predicate();
}
static int fail(const char* message, const AcquisitionController& controller) {
    const auto s = controller.snapshot();
    qCritical("FAIL %s (conn=%d acquisition=%d recording=%d error=%s/%s path=%s)", message,
        static_cast<int>(s.connection), static_cast<int>(s.acquisition), static_cast<int>(s.recording.state),
        qPrintable(s.error.code), qPrintable(s.recording.error.code), qPrintable(s.recording.path));
    return 1;
}
static QString pythonExecutable() {
    const QString configured = qEnvironmentVariable("BF_PYTHON_EXECUTABLE");
    if (!configured.isEmpty()) return configured;
#ifdef Q_OS_WIN
    return QStringLiteral("python");
#else
    return QStringLiteral("python3");
#endif
}
static bool inspectCapture(const QString& root, const VersionInfo& version, const QString& path, bool tcp = false) {
    SidecarClient verifier(root);
    auto hello = verifier.startAndHello(version, 10000);
    if (!hello.ok) return false;
    const auto result = verifier.request("capture_inspect", {{"path", path}}, 30000);
    if (!result.ok || !result.result.value("finalized").toBool() || result.result.value("verification").toString() != "ok") {
        qCritical("inspect failed: %s", qPrintable(QString::fromUtf8(QJsonDocument(result.result).toJson(QJsonDocument::Compact))));
        return false;
    }
    // Verify actual on-disk raw int24 values, absolute indices and descriptor.
    const char* script = R"PY(
import json, math, pathlib, sys
import h5py, numpy as np
p=pathlib.Path(sys.argv[1]); tcp=sys.argv[2]=='tcp'; m=json.loads((p/'manifest.json').read_text())
assert m['finalized'] and m['status']=='complete'
d=m['streams'][0]['descriptor']; start=int(d['record_start_sample_index'])
assert start>0 and d['dtype']=='int24' and len(d['channels'])==(2 if tcp else 4)
with h5py.File(p/'signals.h5','r') as h:
 g=h[f"streams/{d['session_id']}/{d['stream_id']}"]; data=g['samples'][:]; blocks=g['blocks'][:]
 assert data.dtype.kind=='i' and data.dtype.itemsize==4 and len(data)>0 and len(blocks)>0
 assert int(blocks[0]['sample_index'])==start
 for block in blocks:
  first=int(block['sample_index']); offset=int(block['row_offset']); count=int(block['sample_count'])
  for i in range(count):
   for c in range(len(d['channels'])):
    if tcp:
     expected=round(math.sin(2*math.pi*(10*(first+i)*d['rate_den']/d['rate_num']+c*0.1))*1000)
    else:
     v=(300+c*50)*math.sin((first+i)*(2+c*1.5)*2*math.pi/d['rate_num'])
     expected=math.floor(v+0.5) if v>=0 else math.ceil(v-0.5)
    assert int(data[offset+i,c])==expected,(first,i,c,data[offset+i,c],expected)
)PY";
    QProcess process;
    process.start(pythonExecutable(), {"-c", QString::fromUtf8(script), path, tcp ? "tcp" : "local"});
    if (!process.waitForFinished(30000) || process.exitCode() != 0) {
        qCritical("raw readback failed: %s", qPrintable(QString::fromUtf8(process.readAllStandardError())));
        return false;
    }
    return true;
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 2 || argc > 3) { qCritical("expected repo root and optional screenshot directory or --view-only"); return 1; }
    if (!testViewProjection()) { qCritical("FAIL multi-stream display projection"); return 1; }
    if (!testTraceTimeRange()) { qCritical("FAIL shared display time range"); return 1; }
    {
        AcquisitionTrace voltage; voltage.name = "Voltage"; voltage.unit = "mV";
        voltage.points = {{1., .01, false}, {1.04, .03, true}, {1.08, -.02, false}};
        AcquisitionTrace temperature; temperature.name = "Temperature"; temperature.unit = "degC";
        temperature.points = {{.5, 24., false}, {1.3, 25., false}};
        const auto slice = acquisitionWaveformSlice({voltage, temperature});
        WaveformViewState visible; visible.setChannelVisible(0, false);
        const auto model = waveformDisplayModel(slice, visible, 0);
        if (slice.fs != 0 || slice.t0 != .5 || slice.t1 != 1.3 || slice.sampleTimes[0].size() != 4 ||
            !std::isnan(slice.data[0][1]) || slice.sampleTimes[0][1] != 1.04 ||
            model.sampleTimes.size() != 1 || model.sampleTimes[0] != slice.sampleTimes[1] || model.channelUnits[0] != "degC") {
            qCritical("FAIL live waveform coordinates, gap and units"); return 1;
        }
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[2]) == "--view-only") {
        qInfo("PASS multi-stream, shared time axis and fragmented display projection");
        return 0;
    }
    QTemporaryDir configDirectory;
    if (!configDirectory.isValid()) return 1;
    qputenv("BF_ACQUISITION_LOG_DIRECTORY", configDirectory.path().toUtf8());
#ifdef Q_OS_UNIX
    qputenv("XDG_CONFIG_HOME", configDirectory.path().toUtf8());
#else
    QStandardPaths::setTestModeEnabled(true);
#endif
    QCoreApplication::setOrganizationName(QStringLiteral("BoundFreeAcquisitionUITest"));
    QCoreApplication::setApplicationName(QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString root = QString::fromLocal8Bit(argv[1]);
    VersionInfo version; QString message;
    if (!VersionService(root).load(&version, &message)) { qCritical("version: %s", qPrintable(message)); return 1; }
    I18nService i18n(root);
    if (!i18n.loadLocale("en_US", &message)) { qCritical("i18n: %s", qPrintable(message)); return 1; }
    AcquisitionController controller(root, version);
    QString revealedLog; QObject::connect(&controller, &AcquisitionController::logLocationRequested, &controller, [&](const QString& path) { revealedLog = path; });
    QStackedWidget stack;
    auto* ui = new DeviceWorkspace(&controller, &i18n, &stack);
    QString openedRecording;
    ui->captureOpenRequested = [&](const QString& path) { openedRecording = path; };
    stack.addWidget(ui); stack.addWidget(new QWidget(&stack)); stack.resize(900, 620); stack.show();
    const QString screenshots = argc == 3 ? QString::fromLocal8Bit(argv[2]) : QString();
    if (!screenshots.isEmpty() && !QDir().mkpath(screenshots)) return fail("screenshot directory", controller);
    const auto screenshot = [&](const QString& name) {
        if (screenshots.isEmpty()) return true;
        QApplication::processEvents();
        return stack.grab().save(QDir(screenshots).filePath(name));
    };
    if (!screenshot("device-idle.png")) return fail("idle screenshot", controller);
    // The center remains usable in every supported language and at a compact
    // desktop size. Capture the actual Qt render, including the dark theme.
    for (const QString& locale : {QString("zh_CN"), QString("en_US"), QString("ja_JP"), QString("zh_TW"), QString("pseudo")}) {
        if (!i18n.loadLocale(locale, &message)) return fail("center locale", controller);
        ui->retranslateUi();
        for (const char* key : {"app.device.detecting_channels", "app.device.channel_count", "app.device.wifi_power_notice",
                                "app.device.wifi_channels", "error.device.rt_bci_unrecognized_stream",
                                "error.device.channel_count_adjusted"})
            if (i18n.text(key) == QLatin1String(key)) return fail("channel detection translations", controller);
        for (const auto size : {QSize(1280, 800), QSize(900, 620), QSize(760, 560)}) {
            stack.resize(size); QApplication::processEvents();
            if (stack.size() != size) return fail("center minimum size", controller);
            auto* setup = ui->findChild<QPushButton*>("deviceSelectRtBci");
            auto* advanced = ui->findChild<QToolButton*>("deviceConnectionAdvanced");
            if (!setup || !setup->isVisible() || !advanced || !advanced->isVisible() ||
                !stack.rect().contains(QRect(setup->mapTo(&stack, QPoint()), setup->size())))
                return fail("center connection action clipped", controller);
            if (!screenshot(QString("center-%1-%2.png").arg(locale).arg(size.width()))) return fail("center screenshot", controller);
        }
    }
    i18n.loadLocale("zh_CN", &message); ui->retranslateUi(); ui->setDarkTheme(true); stack.resize(1280, 800);
    if (!screenshot("center-zh_CN-dark.png")) return fail("center dark screenshot", controller);
    ui->setDarkTheme(false); i18n.loadLocale("en_US", &message); ui->retranslateUi(); stack.resize(900, 620);
    const auto connection = [&] { return controller.snapshot().connection; };
    const auto acquisition = [&] { return controller.snapshot().acquisition; };
    const auto recording = [&] { return controller.snapshot().recording.state; };
    if (controller.hasActiveTasks()) return fail("starts with no background acquisition", controller);
    auto* pages = ui->findChild<QStackedWidget*>("devicePages");
    auto* selectWifi = ui->findChild<QPushButton*>("deviceSelectRtBci");
    auto* selectSimulator = ui->findChild<QPushButton*>("deviceSelectSimulator");
    auto* selectTcp = ui->findChild<QPushButton*>("deviceSelectTcp");
    auto* back = ui->findChild<QPushButton*>("deviceWifiBack");
    auto* wifiConnect = ui->findChild<QPushButton*>("deviceWifiConnect");
    auto* wifiError = ui->findChild<QLabel*>("deviceWifiError");
    auto* channels = ui->findChild<QComboBox*>("deviceWifiChannels");
    auto* wifiHost = ui->findChild<QLineEdit*>("deviceWifiHost");
    auto* wifiPort = ui->findChild<QSpinBox*>("deviceWifiHttpPort");
    auto* probe = ui->findChild<RtBciWifiProbe*>("deviceWifiProbe");
    if (!pages || !selectWifi || !selectSimulator || !selectTcp || !back || !wifiConnect || !wifiError || !channels || !wifiHost || !wifiPort || !probe ||
        pages->currentWidget()->objectName() != "deviceListPage") return fail("device list controls", controller);
    auto* overview = back;
    auto* tcpConnect = ui->findChild<QPushButton*>("deviceTcpConnect");
    auto* managerDisconnect = ui->findChild<QPushButton*>("deviceManagerDisconnect");
    if (!overview || !tcpConnect || !managerDisconnect) return fail("center navigation controls", controller);
    selectTcp->click();
    if (pages->currentWidget()->objectName() != "deviceTcpPage" || connection() != DeviceConnectionState::Disconnected)
        return fail("TCP settings are read-only until connect", controller);
    if (!screenshot("center-tcp-settings.png")) return fail("TCP settings screenshot", controller);
    overview->click();
    ui->findChild<QToolButton*>("deviceConnectionAdvanced")->setChecked(true);
    if (ui->findChild<QWidget*>("deviceConnectionAlternatives")->isHidden() || connection() != DeviceConnectionState::Disconnected)
        return fail("advanced connection options do not auto-connect", controller);
    overview->click();
    QTcpServer silentDevice;
    if (!silentDevice.listen(QHostAddress::LocalHost, 0)) return fail("probe test listener", controller);
    selectWifi->click(); wifiHost->setText("127.0.0.1"); wifiPort->setValue(silentDevice.serverPort());
    if (!screenshot("device-wifi-waiting.png")) return fail("Wi-Fi waiting screenshot", controller);
    if (!waitUntil([&] { return silentDevice.hasPendingConnections(); })) return fail("read-only probe start", controller);
    const auto firstGeneration = probe->property("activeGeneration").toULongLong();
    RtBciProbeResult response; response.compatible = true; response.channels = 0;
    probe->finished(firstGeneration, response);
    if (pages->currentWidget()->objectName() != "deviceWifiPage" || channels->currentData().toInt() != 0 || !wifiConnect->isEnabled())
        return fail("automatic channel connection enabled", controller);
    if (!stack.rect().contains(QRect(wifiConnect->mapTo(&stack, QPoint()), wifiConnect->size())))
        return fail("Wi-Fi primary action stays visible", controller);
    if (!screenshot("device-wifi-compatible.png")) return fail("Wi-Fi compatible screenshot", controller);
    // Advanced fields live in the same scrollable center, not a second dialog.
    auto* advanced = ui->findChild<QToolButton*>("deviceWifiAdvanced");
    advanced->setChecked(true);
    if (!channels->isVisible() || channels->findData(8) < 0 || channels->findData(16) < 0)
        return fail("advanced fallback channel choices", controller);
    if (!screenshot("center-wifi-advanced.png")) return fail("Wi-Fi advanced screenshot", controller);
    advanced->setChecked(false);
    stack.resize(1100, 720);
    back->click();
    response.channels = 8; probe->finished(firstGeneration, response);
    if (pages->currentWidget()->objectName() != "deviceListPage" || connection() != DeviceConnectionState::Disconnected)
        return fail("late probe after back", controller);
    // Compatibility alone must not override an explicit device-busy error.
    while (silentDevice.hasPendingConnections()) delete silentDevice.nextPendingConnection();
    selectWifi->click(); wifiHost->setText("127.0.0.1"); wifiPort->setValue(silentDevice.serverPort());
    if (!waitUntil([&] { return silentDevice.hasPendingConnections(); })) return fail("second probe start", controller);
    response.error = makeError("device.busy");
    probe->finished(probe->property("activeGeneration").toULongLong(), response);
    channels->setCurrentIndex(channels->findData(8));
    if (wifiConnect->isEnabled() || wifiError->text() != i18n.text("error.device.busy")) return fail("busy probe guard", controller);
    while (silentDevice.hasPendingConnections()) delete silentDevice.nextPendingConnection();
    if (!waitUntil([&] { return silentDevice.hasPendingConnections(); })) return fail("busy probe retries automatically", controller);
    back->click();
    // A module report must not preselect a layout.
    while (silentDevice.hasPendingConnections()) delete silentDevice.nextPendingConnection();
    selectWifi->click(); wifiHost->setText("127.0.0.1"); wifiPort->setValue(silentDevice.serverPort());
    if (!waitUntil([&] { return silentDevice.hasPendingConnections(); })) return fail("third probe start", controller);
    response.error = {};
    probe->finished(probe->property("activeGeneration").toULongLong(), response);
    if (channels->currentData().toInt() != 0 || !wifiConnect->isEnabled()) return fail("reported channel remains diagnostic", controller);
    back->click();

    while (silentDevice.hasPendingConnections()) delete silentDevice.nextPendingConnection();
    selectWifi->click(); wifiHost->setText("127.0.0.1"); wifiPort->setValue(silentDevice.serverPort());
    if (!waitUntil([&] { return silentDevice.hasPendingConnections(); })) return fail("probe before hiding workspace", controller);
    const auto hiddenGeneration = probe->property("activeGeneration").toULongLong();
    stack.setCurrentIndex(1);
    response.channels = 16; probe->finished(hiddenGeneration, response);
    stack.setCurrentIndex(0);
    if (channels->currentData().toInt() != 0 || wifiConnect->isEnabled() || connection() != DeviceConnectionState::Disconnected)
        return fail("hidden workspace cancels probe", controller);
    back->click();

    // Exercise the complete UI path against a local virtual device. Only the
    // explicit connection may issue control requests; opening a saved profile
    // performs read-only detection and leaves the current session intact.
    while (silentDevice.hasPendingConnections()) delete silentDevice.nextPendingConnection();
    bool rejectWifiTcp = true, wifiBadStream = false;
    int wifiReportedChannels = 0, wifiWireChannels = 8;
    QStringList wifiPaths, wifiRequests;
    QPointer<QTcpSocket> wifiData;
    QTimer wifiSamples; wifiSamples.setInterval(20);
    unsigned wifiCounter = 0;
    QObject::connect(&wifiSamples, &QTimer::timeout, &app, [&] {
        if (!wifiData || wifiData->state() != QAbstractSocket::ConnectedState) return;
        QByteArray bytes;
        for (int sample = 0; sample < 5; ++sample) {
            const auto counter = wifiCounter++ & 255;
            for (int bank = 0; bank < wifiWireChannels / 8; ++bank) {
                QByteArray frame(33, '\0'); frame[0] = char(0xa0); frame[1] = char(counter); frame[32] = char(0xc0);
                for (int channel = 0; channel < 8; ++channel) {
                    const unsigned value = 1000 + 10 * (bank * 8 + channel) + wifiCounter;
                    frame[2 + 3 * channel] = char(value >> 16); frame[3 + 3 * channel] = char(value >> 8); frame[4 + 3 * channel] = char(value);
                }
                bytes += frame;
            }
        }
        wifiData->write(bytes);
    });
    QObject::connect(&silentDevice, &QTcpServer::newConnection, &app, [&] {
        while (silentDevice.hasPendingConnections()) {
            auto* socket = silentDevice.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            auto handled = std::make_shared<bool>(false);
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request, handled] {
                if (*handled) return;
                request->append(socket->readAll());
                const int headerEnd = request->indexOf("\r\n\r\n");
                if (headerEnd < 0) return;
                const auto lines = request->left(headerEnd).split('\n');
                const auto first = lines.first().trimmed().split(' ');
                if (first.size() < 2) return;
                int length = 0;
                for (const auto& line : lines) {
                    const auto trimmed = line.trimmed();
                    if (trimmed.toLower().startsWith("content-length:")) length = trimmed.mid(15).trimmed().toInt();
                }
                if (request->size() < headerEnd + 4 + length) return;
                *handled = true;
                const QString path = QString::fromLatin1(first[1]); wifiPaths.push_back(path);
                wifiRequests.push_back(QString::fromLatin1(first[0]) + QLatin1Char(' ') + path);
                const auto body = QJsonDocument::fromJson(request->mid(headerEnd + 4, length)).object();
                QByteArray payload = "{}";
                int httpStatus = 200;
                if (path == QLatin1String("/board")) payload = QJsonDocument(QJsonObject{
                    {"board_connected", true}, {"board_type", "RT-BCI"}, {"num_channels", wifiReportedChannels},
                    {"device_id", "ui-test-device"}}).toJson(QJsonDocument::Compact);
                if (path == QLatin1String("/tcp") && first[0] == "GET") payload = R"({"connected":false,"ip":"0.0.0.0"})";
                if (path == QLatin1String("/tcp") && first[0] == "POST" && rejectWifiTcp) {
                    httpStatus = 400; payload = R"({"error":"Unsupported TCP configuration"})";
                }
                if (path == QLatin1String("/tcp") && first[0] == "POST" && !rejectWifiTcp) {
                    wifiData = new QTcpSocket(&silentDevice);
                    wifiData->connectToHost(body.value("ip").toString(), static_cast<quint16>(body.value("port").toInt()));
                }
                if (path == QLatin1String("/stream/start")) {
                    wifiCounter = 0;
                    if (wifiBadStream) QTimer::singleShot(100, &wifiSamples, [&] {
                        if (wifiData) wifiData->write(QByteArray(12, '\0'));
                    });
                    else QTimer::singleShot(250, &wifiSamples, [&] { wifiSamples.start(); });
                }
                if (path == QLatin1String("/stream/stop")) wifiSamples.stop();
                socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(httpStatus) + " test\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                              QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                socket->disconnectFromHost();
            });
        }
    });
    const QString profileFile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("device-profiles.json");
    if (QFile::exists(profileFile)) return fail("profile isolation", controller);
    QTcpServer portReservation;
    if (!portReservation.listen(QHostAddress::LocalHost, 0)) return fail("data port reservation", controller);
    const int listenPort = portReservation.serverPort(); portReservation.close();
    auto* wifiListen = ui->findChild<QSpinBox*>("deviceWifiListenPort");
    auto* wifiStatus = ui->findChild<QLabel*>("deviceWifiStatus");
    if (!wifiListen || !wifiStatus) return fail("Wi-Fi advanced controls", controller);
    selectWifi->click(); wifiHost->setText("127.0.0.1"); wifiPort->setValue(silentDevice.serverPort()); wifiListen->setValue(listenPort);
    if (!waitUntil([&] { return wifiStatus->text() == i18n.text("app.device.wifi_response"); })) return fail("real read-only probe", controller);
    if (!wifiRequests.contains(QStringLiteral("GET /board")) ||
        std::any_of(wifiRequests.cbegin(), wifiRequests.cend(), [](const QString& request) { return !request.startsWith(QLatin1String("GET ")); }) ||
        channels->currentData().toInt() != 0 || !wifiConnect->isEnabled() || QFile::exists(profileFile))
        return fail("probe is read-only and does not save", controller);
    channels->setCurrentIndex(channels->findData(8)); wifiConnect->click();
    if (!waitUntil([&] { return connection() == DeviceConnectionState::Faulted; }, 15000)) return fail("callback rejection", controller);
    if (!waitUntil([&] { return ui->findChild<QLabel*>("deviceManagerState")->text() == i18n.text("app.device.status_connection_failed"); }) ||
        !wifiError->text().contains("POST /tcp") || !wifiError->text().contains("HTTP 400") ||
        !wifiError->text().contains("Unsupported TCP configuration")) {
        qCritical() << "Connection failure UI:" << ui->findChild<QLabel*>("deviceManagerState")->text() << wifiError->text()
                    << controller.snapshot().error.params;
        return fail("actionable connection failure", controller);
    }
    controller.requestOpenLogs();
    if (revealedLog.isEmpty() || revealedLog != controller.snapshot().diagnosticLogPath || !QFile::exists(revealedLog))
        return fail("failed connection retains an accessible session log", controller);
    if (!screenshot("device-wifi-connection-failed.png")) return fail("connection failure screenshot", controller);
    auto* copyDiagnostics = ui->findChild<QPushButton*>("deviceWifiCopyDiagnostics");
    if (!copyDiagnostics || copyDiagnostics->isHidden()) return fail("copy diagnostic control", controller);
    copyDiagnostics->click();
    const auto diagnostic = QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8()).object();
    if (diagnostic.value("code") != "device.rt_bci_http" ||
        diagnostic.value("params").toObject().value("path") != "/tcp" ||
        !diagnostic.value("params").toObject().value("tcp_request").toObject().value("delimiter").toBool()) return fail("copied diagnostics", controller);
    rejectWifiTcp = false;
    // The module reports 8 while the wire has 16 channels; leave Auto selected.
    wifiReportedChannels = 8; wifiWireChannels = 16;
    channels->setCurrentIndex(channels->findData(0));
    wifiConnect->click();
    if (!waitUntil([&] { return connection() == DeviceConnectionState::Connected; }, 20000) ||
        connection() != DeviceConnectionState::Connected) return fail("virtual Wi-Fi connect", controller);
    if (!waitUntil([&] {
        return controller.snapshot().connectionPhase == "detecting" &&
            ui->findChild<QLabel*>("deviceManagerState")->text() == i18n.text("app.device.detecting_channels");
    }) || controller.snapshot().totalChannels != 0) return fail("recognition phase before samples", controller);
    if (!waitUntil([&] { return pages->currentWidget()->objectName() == "deviceLivePage" && acquisition() == DeviceAcquisitionState::Streaming; }, 20000) ||
        !wifiPaths.contains("/stream/start")) return fail("Wi-Fi connects directly to live signals", controller);
    if (!waitUntil([&] { const auto view = controller.snapshot(); return view.totalChannels == 16 && view.traces.size() == 16 && view.samples >= 20; }))
        return fail("automatic 16 channels override module's 8-channel report", controller);
    if (!waitUntil([&] { return ui->findChild<QLabel*>("deviceManagerState")->text().contains(
            i18n.text("app.device.channel_count", {{"count", 16}})); }))
        return fail("recognized channel count shown", controller);
    if (!screenshot("device-wifi-selected16-reported8.png")) return fail("16-channel compatibility screenshot", controller);
    if (!ui->findChild<QWidget*>("deviceLiveDetails")->isHidden() || !ui->findChild<WaveformWidget*>("deviceTraceView"))
        return fail("live defaults reuse waveform and hide advanced settings", controller);
    controller.stopAcquisition();
    if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Idle; })) return fail("Wi-Fi pause before profile management", controller);
    QFile saved(profileFile);
    if (!saved.open(QIODevice::ReadOnly)) return fail("successful profile save", controller);
    const QByteArray savedBytes = saved.readAll(); saved.close();
    const auto savedObject = QJsonDocument::fromJson(savedBytes).object();
    const auto savedProfiles = savedObject.value("profiles").toArray();
    if (savedObject.value("schema").toInt() != 1 || savedProfiles.size() != 1 ||
        savedProfiles.first().toObject().value("adapter").toString() != "rt-bci.wifi" ||
        savedProfiles.first().toObject().value("channels").toInt(-1) != 0 || savedBytes.toLower().contains("password"))
        return fail("safe profile schema", controller);
    for (int savedChannels : {0, 8, 16}) {
        auto item = savedProfiles.first().toObject(); item.insert("channels", savedChannels);
        QFile legacy(profileFile);
        if (!legacy.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            legacy.write(QJsonDocument(QJsonObject{{"schema", 1}, {"profiles", QJsonArray{item}}}).toJson()) < 0)
            return fail("saved profile fixture", controller);
        legacy.close();
        DeviceWorkspace restored(&controller, &i18n);
        auto* reconnect = restored.findChild<QPushButton*>("deviceRecentConnect");
        if (!reconnect) return fail("saved profile loaded", controller);
        reconnect->click();
        auto* restoredChannels = restored.findChild<QComboBox*>("deviceWifiChannels");
        if (!restoredChannels || restoredChannels->currentData().toInt() != savedChannels)
            return fail("saved automatic and legacy channel options", controller);
    }
    QJsonArray mixedProfiles;
    mixedProfiles.append(QJsonObject{{"id", "unknown"}, {"adapter", "future.adapter"}});
    for (int index = 0; index < 17; ++index) {
        auto item = savedProfiles.first().toObject(); item.insert("id", QStringLiteral("profile-%1").arg(index)); mixedProfiles.append(item);
    }
    QFile mixedFile(profileFile);
    if (!mixedFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        mixedFile.write(QJsonDocument(QJsonObject{{"schema", 1}, {"profiles", mixedProfiles}}).toJson()) < 0)
        return fail("invalid profile fixture", controller);
    mixedFile.close();
    {
        DeviceWorkspace restored(&controller, &i18n);
        auto* notice = restored.findChild<QLabel*>("deviceProfileNotice");
        if (!notice || notice->isHidden() || restored.findChildren<QPushButton*>("deviceRecentConnect").size() != 16)
            return fail("invalid and excess profiles", controller);
    }
    if (!mixedFile.open(QIODevice::WriteOnly | QIODevice::Truncate) || mixedFile.write(savedBytes) != savedBytes.size())
        return fail("restore profile fixture", controller);
    mixedFile.close();
    ui->findChild<QPushButton*>("deviceLiveList")->click();
    auto* recent = ui->findChild<QPushButton*>("deviceRecentConnect");
    if (!recent) return fail("recent device", controller);
    const int beforeBrowsing = wifiPaths.size();
    recent->click();
    if (connection() != DeviceConnectionState::Connected || wifiPaths.size() != beforeBrowsing ||
        pages->currentWidget()->objectName() != "deviceWifiPage") return fail("browsing keeps connection", controller);
    back->click();
    selectTcp->click();
    if (connection() != DeviceConnectionState::Connected || wifiPaths.size() != beforeBrowsing ||
        pages->currentWidget()->objectName() != "deviceTcpPage") return fail("TCP settings preserve existing Wi-Fi connection", controller);
    overview->click();
    auto* simulatorProfile = ui->findChild<QComboBox*>("deviceSimulatorProfile");
    if (!simulatorProfile || !simulatorProfile->currentData().toBool()) return fail("default ICA profile", controller);
    // Preserve the int24 recording fixture; the ICA preset has its own float32 replay test.
    simulatorProfile->setCurrentIndex(1);
    selectSimulator->click();
    if (!waitUntil([&] { return connection() == DeviceConnectionState::Connected; })) return fail("connect", controller);
    if (!waitUntil([&] { return pages->currentWidget()->objectName() == "deviceLivePage" && acquisition() == DeviceAcquisitionState::Streaming; }))
        return fail("simulator connects directly to live signals", controller);
    controller.stopAcquisition();
    if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Idle; })) return fail("pause simulator for configuration", controller);
    controller.configureSimulator(500);
    if (!waitUntil([&] { auto s = controller.snapshot(); return s.connection == DeviceConnectionState::Connected && s.sampleRate == 500; }))
        return fail("configure", controller);
    for (int i = 0; i < 20; ++i) {
        controller.startAcquisition(); controller.startAcquisition();
        if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Streaming; })) return fail("repeated start", controller);
        controller.stopAcquisition(); controller.stopAcquisition();
        if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Idle; })) return fail("repeated stop", controller);
    }
    controller.startAcquisition();
    if (!waitUntil([&] { return controller.snapshot().samples >= 500; })) return fail("stream samples", controller);
    QTemporaryDir directory;
    if (!directory.isValid()) return fail("temporary directory", controller);
    controller.startRecording(directory.filePath("does-not-exist"));
    if (!waitUntil([&] { return recording() == CaptureRecordState::Failed; }, 30000)) return fail("failure recovery setup", controller);
    if (!waitUntil([&] { auto* b = ui->findChild<QPushButton*>("deviceRecord"); return b && b->isEnabled(); })) return fail("record button recovery", controller);
    controller.startRecording(directory.path());
    controller.stopRecording();
    if (!waitUntil([&] { auto r = recording(); return r == CaptureRecordState::Off || r == CaptureRecordState::Partial; }, 30000))
        return fail("cancel opening", controller);
    for (int round = 0; round < 2; ++round) {
        controller.startRecording(directory.path());
        if (!waitUntil([&] { return recording() == CaptureRecordState::Recording; }, 30000)) return fail("record start", controller);
        auto* freeze = ui->findChild<QPushButton*>("deviceFreeze");
        if (!freeze || !freeze->isEnabled()) return fail("freeze unavailable", controller);
        freeze->click();
        if (round == 0 && !screenshot("device-frozen-recording.png")) return fail("recording screenshot", controller);
        const auto before = controller.snapshot().samples;
        auto* listButton = ui->findChild<QPushButton*>("deviceLiveList");
        auto* currentButton = ui->findChild<QPushButton*>("deviceViewCurrent");
        if (!listButton || !currentButton) return fail("live navigation controls", controller);
        listButton->click();
        if (pages->currentWidget()->objectName() != "deviceListPage" || selectWifi->isEnabled() || selectSimulator->isEnabled() || selectTcp->isEnabled())
            return fail("device switching locked during recording", controller);
        if (managerDisconnect->isEnabled() || selectTcp->isEnabled())
            return fail("center locks disconnect during recording", controller);
        if (round == 0 && !screenshot("center-recording-in-background.png")) return fail("center background screenshot", controller);
        if (round == 0) {
            auto* recentName = ui->findChild<QLineEdit*>("deviceRecentName");
            if (!recentName || !recentName->isEnabled()) return fail("profile rename while recording", controller);
            recentName->setText(QStringLiteral("UI test device"));
            QMetaObject::invokeMethod(recentName, "editingFinished", Qt::DirectConnection);
            QFile renamed(profileFile);
            if (!renamed.open(QIODevice::ReadOnly) ||
                QJsonDocument::fromJson(renamed.readAll()).object().value("profiles").toArray().first().toObject().value("name").toString() != "UI test device")
                return fail("profile rename persistence", controller);
        }
        if (!waitUntil([&] { return controller.snapshot().samples >= before + 100; }) || recording() != CaptureRecordState::Recording)
            return fail("device list preserves recording", controller);
        currentButton->click();
        stack.setCurrentIndex(1);
        if (!waitUntil([&] { return controller.snapshot().samples >= before + 250; })) return fail("background acquisition", controller);
        stack.setCurrentIndex(0); freeze->click();
        if (round == 0) controller.stopRecording(); else controller.stopAcquisition();
        if (!waitUntil([&] { auto r = recording(); return r == CaptureRecordState::Saved || r == CaptureRecordState::Partial || r == CaptureRecordState::Failed; }, 30000))
            return fail("finalize timeout", controller);
        auto result = controller.snapshot();
        if (result.recording.state != CaptureRecordState::Saved || result.recording.writtenSamples == 0 ||
            !inspectCapture(root, version, result.recording.path)) return fail("record readback", controller);
        auto* viewRecording = ui->findChild<QPushButton*>("deviceOpenRecording");
        if (!waitUntil([&] { return viewRecording && viewRecording->isVisible() && viewRecording->isEnabled(); }))
            return fail("saved recording entry", controller);
        viewRecording->click();
        if (openedRecording != result.recording.path) return fail("saved recording path", controller);
    }
    if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Idle; })) return fail("stop with recording", controller);
    controller.disconnectDevice();
    if (!waitUntil([&] { return connection() == DeviceConnectionState::Disconnected; })) return fail("disconnect", controller);

    QProcess server;
    server.setWorkingDirectory(root);
    server.start(pythonExecutable(), {"-m", "tools.bfsp_simulator", "--port", "0"});
    if (!server.waitForStarted(10000) || !server.waitForReadyRead(10000)) return fail("TCP simulator start", controller);
    const QString serverOutput = QString::fromUtf8(server.readAllStandardOutput());
    const auto match = QRegularExpression("127\\.0\\.0\\.1['\\\"],\\s*(\\d+)").match(serverOutput);
    if (!match.hasMatch()) { qCritical("server output: %s", qPrintable(serverOutput)); return fail("TCP simulator port", controller); }
    auto* source = ui->findChild<QComboBox*>("deviceSource");
    auto* port = ui->findChild<QSpinBox*>("deviceTcpPort");
    auto* connectButton = ui->findChild<QPushButton*>("deviceConnect");
    auto* acquireButton = ui->findChild<QPushButton*>("deviceAcquire");
    auto* settingsHint = ui->findChild<QLabel*>("deviceSettingsHint");
    if (!source || !port || !connectButton || !acquireButton || !settingsHint) return fail("TCP controls", controller);
    const auto activeSettingsHint = [&] {
        return settingsHint->text() == QStringLiteral("Stop acquisition before changing settings.");
    };
    ui->findChild<QPushButton*>("deviceLiveList")->click(); selectTcp->click();
    if (pages->currentWidget()->objectName() != "deviceTcpPage") return fail("TCP navigation", controller);
    port->setValue(match.captured(1).toInt()); tcpConnect->click();
    if (!waitUntil([&] { return connection() == DeviceConnectionState::Connected; }, 20000)) return fail("TCP connect", controller);
    if (!waitUntil([&] { return pages->currentWidget()->objectName() == "deviceLivePage" && acquisition() == DeviceAcquisitionState::Streaming; }, 20000))
        return fail("TCP connects directly to live signals", controller);
    controller.stopAcquisition();
    if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Idle; }, 20000)) return fail("pause TCP before configuration", controller);
    if (recording() != CaptureRecordState::Off) return fail("previous source recording state", controller);
    auto* rate = ui->findChild<QComboBox*>("deviceSampleRate");
    auto* apply = ui->findChild<QPushButton*>("deviceApply");
    auto* tcpFacade = controller.findChild<AcquisitionTcpSource*>();
    if (!rate || !apply || !tcpFacade || !waitUntil([&] { return rate->isEnabled(); })) return fail("TCP configuration capability", controller);
    int sourceNotifications = 0;
    QObject notificationObserver;
    QObject::connect(tcpFacade, &AcquisitionTcpSource::stateChanged, &notificationObserver, [&] { ++sourceNotifications; });
    const auto configurationLocked = [&] {
        const auto s = controller.snapshot();
        return s.configurationInProgress && !s.canConfigure && !rate->isEnabled() && !apply->isEnabled() &&
            !acquireButton->isEnabled() && !connectButton->isEnabled();
    };
    const auto configurationReady = [&] {
        const auto s = controller.snapshot();
        return !s.configurationInProgress && s.canConfigure && rate->isEnabled() && acquireButton->isEnabled() &&
            connectButton->isEnabled() && settingsHint->text() == i18n.text("app.device.tcp_settings_available");
    };
    // Two identical failures verify that a previous identical error cannot
    // masquerade as the completion of a new configuration request.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto before = tcpFacade->snapshot().configCompletion;
        const int notificationsBefore = sourceNotifications;
        controller.configureSampleRate(0);
        controller.configureSampleRate(0); // Duplicate must not reach the worker.
        if (!configurationLocked()) return fail("TCP configuration pending", controller);
        if (!waitWithoutGuiEvents([&] { return tcpFacade->snapshot().configCompletion != before; }, 20000))
            return fail("TCP configuration failure completion", controller);
        if (sourceNotifications != notificationsBefore || !configurationLocked())
            return fail("TCP configuration failure processed GUI events", controller);
        QApplication::processEvents();
        if (!waitUntil(configurationReady) ||
            controller.snapshot().error.code != "capability.unsupported" || tcpFacade->snapshot().configCompletion != before + 1)
            return fail("TCP repeated configuration failure", controller);
    }
    rate->setCurrentIndex(rate->findData(500));
    if (!apply->isEnabled()) return fail("TCP configuration selection", controller);
    const auto beforeConfig = tcpFacade->snapshot();
    const int notificationsBeforeConfig = sourceNotifications;
    apply->click();
    controller.configureSampleRate(500); // Guard a second click while the first is pending.
    if (!configurationLocked()) return fail("TCP configuration pending projection", controller);
    controller.startAcquisition(); // Starting must remain locked during configuration.
    if (acquisition() != DeviceAcquisitionState::Idle) return fail("TCP start during configuration", controller);
    if (!waitWithoutGuiEvents([&] { return tcpFacade->snapshot().configCompletion != beforeConfig.configCompletion; }, 20000))
        return fail("TCP coalesced configuration completion", controller);
    const auto completedConfig = tcpFacade->snapshot();
    if (completedConfig.configCompletion != beforeConfig.configCompletion + 1 ||
        completedConfig.configRevision <= beforeConfig.configRevision || completedConfig.exactRate.numerator != 500 ||
        sourceNotifications != notificationsBeforeConfig || !configurationLocked()) return fail("TCP coalesced configuration projection", controller);
    QApplication::processEvents();
    if (!waitUntil(configurationReady, 20000) || apply->isEnabled() ||
        controller.snapshot().exactRate.numerator != 500 || controller.snapshot().exactRate.denominator != 1)
        return fail("TCP configuration apply", controller);
    // Identical successful requests must also finish: neither an unchanged
    // rate nor a previously observed success can acknowledge the next request.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto before = tcpFacade->snapshot().configCompletion;
        const int notificationsBefore = sourceNotifications;
        controller.configureSampleRate(500);
        controller.configureSampleRate(500);
        if (!configurationLocked() ||
            !waitWithoutGuiEvents([&] { return tcpFacade->snapshot().configCompletion != before; }, 20000) ||
            sourceNotifications != notificationsBefore || !configurationLocked())
            return fail("TCP identical configuration coalescing", controller);
        QApplication::processEvents();
        if (!waitUntil(configurationReady, 20000) || apply->isEnabled() ||
            !controller.snapshot().error.code.isEmpty() || tcpFacade->snapshot().configCompletion != before + 1)
            return fail("TCP identical configuration completion", controller);
    }
    const auto beforeCancel = tcpFacade->snapshot().acquisitionCompletion;
    const int notificationsBeforeCancel = sourceNotifications;
    controller.startAcquisition();
    if (!activeSettingsHint()) return fail("TCP starting settings hint", controller);
    controller.startAcquisition(); controller.stopAcquisition();
    if (acquisition() != DeviceAcquisitionState::Stopping) return fail("TCP pending cancel state", controller);
    if (!activeSettingsHint()) return fail("TCP stopping settings hint", controller);
    if (!waitWithoutGuiEvents([&] { return tcpFacade->snapshot().acquisitionCompletion != beforeCancel; }, 20000) ||
        sourceNotifications != notificationsBeforeCancel || acquisition() != DeviceAcquisitionState::Stopping)
        return fail("TCP coalesced cancellation completion", controller);
    QApplication::processEvents();
    if (!waitUntil(configurationReady, 20000) || acquisition() != DeviceAcquisitionState::Idle ||
        tcpFacade->snapshot().acquisitionCompletion != beforeCancel + 1)
        return fail("TCP cancel start cleanup", controller);
    if (!waitUntil([&] { return acquireButton->isEnabled(); })) return fail("TCP retry enabled", controller);
    acquireButton->click();
    if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Streaming && controller.snapshot().samples >= 100; }, 20000))
        return fail("TCP stream", controller);
    const auto tcpView = controller.snapshot();
    if (tcpView.traces.size() != 2 || tcpView.exactRate.numerator != 500 || tcpView.exactRate.denominator != 1)
        return fail("TCP descriptor projection", controller);
    if (!activeSettingsHint()) return fail("TCP streaming settings hint", controller);
    if (!screenshot("device-tcp-streaming.png")) return fail("TCP screenshot", controller);
    controller.startRecording(directory.path());
    if (!waitUntil([&] { return recording() == CaptureRecordState::Recording; }, 30000)) return fail("TCP record start", controller);
    const auto beforeTcp = controller.snapshot().samples;
    if (!waitUntil([&] { return controller.snapshot().samples >= beforeTcp + 100; })) return fail("TCP recording data", controller);
    controller.stopRecording();
    if (!waitUntil([&] { return recording() == CaptureRecordState::Saved || recording() == CaptureRecordState::Failed ||
                                recording() == CaptureRecordState::Partial; }, 30000)) return fail("TCP finalize", controller);
    const auto tcpRecording = controller.snapshot().recording;
    if (tcpRecording.state != CaptureRecordState::Saved || !inspectCapture(root, version, tcpRecording.path, true))
        return fail("TCP record readback", controller);
    const auto beforeStop = tcpFacade->snapshot().acquisitionCompletion;
    const int notificationsBeforeStop = sourceNotifications;
    controller.stopAcquisition(); controller.stopAcquisition();
    controller.startRecording(directory.path()); // A projected stop must block a new recording.
    if (acquisition() != DeviceAcquisitionState::Stopping || recording() != CaptureRecordState::Saved ||
        controller.snapshot().recording.path != tcpRecording.path || acquireButton->isEnabled() ||
        ui->findChild<QPushButton*>("deviceRecord")->isEnabled() || !activeSettingsHint()) return fail("TCP stop interaction guards", controller);
    if (!waitWithoutGuiEvents([&] { return tcpFacade->snapshot().acquisitionCompletion != beforeStop; }, 20000) ||
        sourceNotifications != notificationsBeforeStop || acquisition() != DeviceAcquisitionState::Stopping)
        return fail("TCP coalesced stop completion", controller);
    QApplication::processEvents();
    if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Idle && configurationReady(); }, 20000) ||
        tcpFacade->snapshot().acquisitionCompletion != beforeStop + 1) return fail("TCP stop controls recovery", controller);
    acquireButton->click();
    if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Streaming && controller.snapshot().samples >= 100; }, 20000))
        return fail("TCP restart after coalesced stop", controller);
    controller.startRecording(directory.path());
    if (!waitUntil([&] { return recording() == CaptureRecordState::Recording; }, 30000)) return fail("TCP shutdown recording", controller);
    const auto beforeShutdown = controller.snapshot().samples;
    if (!waitUntil([&] { return controller.snapshot().samples >= beforeShutdown + 100; })) return fail("TCP shutdown data", controller);
    bool shutdownDone = false, shutdownSucceeded = false;
    QObject::connect(&controller, &AcquisitionController::shutdownFinished, &controller, [&](bool success) {
        shutdownDone = true; shutdownSucceeded = success;
    });
    controller.requestShutdown();
    if (!waitUntil([&] { return shutdownDone; }, 30000) || !shutdownSucceeded || acquisition() != DeviceAcquisitionState::Idle ||
        recording() != CaptureRecordState::Saved) return fail("TCP stop and save on shutdown", controller);
    connectButton->click();
    if (!waitUntil([&] { return connection() == DeviceConnectionState::Disconnected; }, 20000)) return fail("TCP disconnect", controller);
    ui->findChild<QPushButton*>("deviceLiveList")->click();
    auto* remove = ui->findChild<QPushButton*>("deviceRecentRemove");
    if (!remove) return fail("saved profile removal control", controller);
    remove->click(); QApplication::processEvents();
    QFile removedProfile(profileFile);
    if (!removedProfile.open(QIODevice::ReadOnly) ||
        !QJsonDocument::fromJson(removedProfile.readAll()).object().value("profiles").toArray().isEmpty() ||
        ui->findChild<QPushButton*>("deviceRecentConnect")) return fail("saved profile removal persists", controller);

    // A detected layout overrides a conflicting fallback with a visible,
    // non-blocking warning; an unrecognizable stream shows recovery guidance.
    selectWifi->click(); wifiHost->setText("127.0.0.1"); wifiPort->setValue(silentDevice.serverPort()); wifiListen->setValue(listenPort);
    if (!waitUntil([&] { return wifiConnect->isEnabled(); })) return fail("warning probe", controller);
    channels->setCurrentIndex(channels->findData(8)); wifiConnect->click();
    if (!waitUntil([&] { return controller.snapshot().totalChannels == 16 &&
                                controller.snapshot().warning.code == "device.channel_count_adjusted"; }, 15000) ||
        !waitUntil([&] { return ui->findChild<QLabel*>("deviceRecordStatus")->text().contains(
                                i18n.text("error.device.channel_count_adjusted")); }))
        return fail("detected count warning shown", controller);
    controller.stopAcquisition();
    if (!waitUntil([&] { return acquisition() == DeviceAcquisitionState::Idle; })) return fail("stop warning case", controller);
    controller.disconnectDevice();
    if (!waitUntil([&] { return connection() == DeviceConnectionState::Disconnected; })) return fail("disconnect warning case", controller);
    ui->findChild<QPushButton*>("deviceLiveList")->click();
    wifiBadStream = true;
    selectWifi->click(); wifiHost->setText("127.0.0.1"); wifiPort->setValue(silentDevice.serverPort()); wifiListen->setValue(listenPort);
    if (!waitUntil([&] { return wifiConnect->isEnabled(); })) return fail("unrecognized probe", controller);
    channels->setCurrentIndex(channels->findData(0)); wifiConnect->click();
    if (!waitUntil([&] { return controller.snapshot().error.code == "device.rt_bci_unrecognized_stream"; }, 10000) ||
        !waitUntil([&] { return ui->findChild<QLabel*>("deviceRecordStatus")->text().contains(
                                i18n.text("error.device.rt_bci_unrecognized_stream")); }))
        return fail("unrecognized stream guidance shown", controller);
    ui->findChild<QPushButton*>("deviceLiveList")->click();
    selectWifi->click();
    if (!wifiError->text().contains(i18n.text("error.device.rt_bci_unrecognized_stream")) ||
        copyDiagnostics->isHidden()) return fail("unrecognized diagnostics on connection page", controller);
    copyDiagnostics->click();
    if (QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8()).object().value("code")
        != "device.rt_bci_unrecognized_stream") return fail("copy unrecognized diagnostics", controller);
    server.terminate();
    if (!server.waitForFinished(3000)) { server.kill(); server.waitForFinished(3000); }
    qInfo("PASS: shared multi-stream time axis; simulator 20 start/stop cycles; coalesced TCP configuration/cancel/stop; failure retry; frozen/background acquisition; local and TCP stdio readback; TCP stop/save on shutdown");
    return 0;
}
