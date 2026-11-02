#include "controller/acquisition_diagnostic_log.h"
#include "controller/acquisition_controller.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QThread>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace bf;
using namespace bf::acquisition;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x); } while(false)
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir root; CHECK(root.isValid()); qputenv("BF_ACQUISITION_LOG_DIRECTORY", root.path().toUtf8());
        AcquisitionDiagnosticLog log;
        CHECK(log.open("simulator.in_process", true, {{"password", "never-expose"}, {"note", "token=never-expose"}}));
        StreamDescriptor d; d.key.stream_id = 1; d.dtype = DType::Int24;
        ChannelDescriptor ch; ch.name = "CH1"; ch.unit = Unit::Microvolt; ch.scale = .1; ch.valid_adc_bits = 24; d.channels = {ch};
        const std::int32_t a = 5000000, b = 5000004;
        log.observeInt24(&a, d); log.observeInt24(&b, d); log.flushStatistics();
        MemoryBudget budget(1024*1024);
        d.key.stream_id = 2; d.dtype = DType::Float64; d.channels[0].valid_adc_bits.reset();
        SampleBlock block; block.channel_count = 1; block.sample_count = 4;
        block.stream = d.key;
        block.storage = SampleStorage::fromFloat64({1., 3., 5., 7.}, budget);
        log.observe(block, d); log.flushStatistics(); log.write("stream_stopped");
        const auto path = log.path(); log.close(); CHECK(log.path() == path);
        QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); CHECK(!bytes.contains("never-expose"));
        bool counts = false, floats = false, stopped = false;
        QString session;
        for (const auto& line : bytes.split('\n')) {
            if (line.isEmpty()) continue;
            const auto event = QJsonDocument::fromJson(line).object(); CHECK(!event.isEmpty());
            CHECK(event.value("schema") == 1 && event.value("adapter") == "simulator.in_process" && event.value("simulated").toBool());
            if (session.isEmpty()) session = event.value("log_session_id").toString();
            CHECK(event.value("log_session_id") == session);
            if (event.value("kind") == "stream_stopped") stopped = true;
            if (event.value("kind") != "signal_stats") continue;
            const auto c = event.value("channels").toArray()[0].toObject();
            CHECK(c.value("mean").isDouble());
            if (event.value("stream_id") == 1) { CHECK(c.value("peak_to_peak") == 4 && c.value("mean") == 5000002 && c.value("stddev") == 2); counts = true; }
            if (event.value("stream_id") == 2) { CHECK(c.value("mean") == 4 && c.value("nonfinite_samples") == "0" && c.value("rail_fraction").isNull()); floats = true; }
        }
        CHECK(counts && floats && stopped);
        CHECK(log.open("rt-bci.wifi", false)); CHECK(log.path() != path);
        const auto boundedPath = log.path();
        for (int i=0; i<1200; ++i) log.write("wire_warning", {{"raw_hex", QString(8000, 'a')}});
        log.close(); CHECK(QFileInfo(boundedPath).size() <= 8*1024*1024+64*1024);
        QFile bounded(boundedPath); CHECK(bounded.open(QIODevice::ReadOnly)); CHECK(bounded.readAll().contains("log_limit"));
        QFile notDirectory(root.path()+"/blocked"); CHECK(notDirectory.open(QIODevice::WriteOnly)); notDirectory.close();
        qputenv("BF_ACQUISITION_LOG_DIRECTORY", notDirectory.fileName().toUtf8());
        CHECK(!log.open("simulator.in_process", true) && log.path().isEmpty());
        VersionInfo version; version.ipcProtocolVersion = 1;
        AcquisitionController controller(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath(), version);
        const auto until = [&](const auto& predicate) {
            QElapsedTimer timer; timer.start();
            while (!predicate() && timer.elapsed() < 4000) { QCoreApplication::processEvents(); QThread::msleep(1); }
            return predicate();
        };
        controller.connectSimulator(); CHECK(until([&] { return controller.snapshot().connection == DeviceConnectionState::Connected; }));
        controller.startAcquisition(); CHECK(until([&] { return controller.snapshot().acquisition == DeviceAcquisitionState::Streaming && controller.snapshot().samples > 0; }));
        CHECK(controller.diagnosticLogPath().isEmpty() && controller.snapshot().error.code.isEmpty());
        controller.stopAcquisition(); CHECK(until([&] { return !controller.hasActiveTasks(); }));
        std::cout << "ACQUISITION_DIAGNOSTIC_LOG_TEST PASS schema provenance integer_float_stats redaction tail limits unwritable_capture_continues\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
