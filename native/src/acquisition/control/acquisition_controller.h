#pragma once

#include "acquisition/control/acquisition_state.h"
#include "render/signal_processor.h"
#include <QJsonArray>
#include <mutex>

#include <QObject>
#include <QString>
#include <cstdint>
#include <memory>
#include <vector>

namespace bf {

class AcquisitionTcpSource;
class AcquisitionWifiSource;
class AcquisitionSource;
struct RtBciWifiOptions;

// Commands and state signals belong to the GUI thread. Acquisition, decoding,
// display reduction and recorder I/O run off-thread. snapshot() copies only a
// bounded, already reduced view and never removes data needed for recording.
class AcquisitionController final : public QObject {
    Q_OBJECT
public:
    AcquisitionController(QString repoRoot, VersionInfo version, QObject* parent = nullptr);
    ~AcquisitionController() override;

    AcquisitionViewSnapshot snapshot() const;
    bool hasActiveTasks() const;
    QString diagnosticLogPath() const;
    void requestOpenLogs();
    void connectSimulator(bool icaMix = false);
    void connectTcp(const QString& host, quint16 port, const QString& expectedDeviceId = {});
    void connectWifi(const RtBciWifiOptions& options);
    void disconnectDevice();
    void configureSimulator(int sampleRate);
    void configureSampleRate(int sampleRate);
    void startAcquisition();
    void stopAcquisition();
    void startRecording(const QString& outputDirectory);
    void stopRecording();
    void requestShutdown();
    QJsonArray signalStreams() const;
    QString signalSourceId() const;
    void requestSignalWindow(const QString& stream, const ProcessingViewState&, bool causal, double seconds = 2);
    void cancelSignalWindow();
    // Independent raw snapshot: does not change the browser, filters or recording.
    void requestRawSignalWindow(quint64 requestId, const QString& stream, double seconds = 10);

signals:
    void stateChanged();
    void signalWindowReady(const bf::WaveformSlice& raw, const bf::WaveformSlice& processed);
    void signalWindowFailed(const bf::AppError& error);
    void rawSignalWindowReady(quint64 requestId, const bf::WaveformSlice& raw);
    void rawSignalWindowFailed(quint64 requestId, const bf::AppError& error);
    void shutdownFinished(bool success);
    void logLocationRequested(const QString& path);

private:
    bool selectSource(AcquisitionSource* source);
    QString lastDiagnosticLogPath_;
    struct State;
    std::shared_ptr<State> state_;
    CaptureRecorder* recorder_ = nullptr;
    AcquisitionTcpSource* tcpSource_ = nullptr;
    AcquisitionWifiSource* wifiSource_ = nullptr;
    AcquisitionSource* activeSource_ = nullptr;
    bool sourceOpenPending_ = false;
    bool sourceClosePending_ = false;
    bool sourceStartPending_ = false;
    bool sourceStopPending_ = false;
    bool sourceConfigurePending_ = false;
    quint64 sourceConfigStartCompletion_ = 0;
    quint64 sourceConnectionRevision_ = 0;
    quint64 sourceSelectionRevision_ = 0;
    quint64 sourceDisconnectionCompletion_ = 0;
    quint64 sourceStartCompletion_ = 0;
    quint64 sourceStopCompletion_ = 0;
    QThread* worker_ = nullptr;
    bool signalReadBusy_ = false;
    bool signalProcessingEnabled_ = false;
    QString lastSignalStream_;
    ProcessingViewState lastSignalSettings_;
    bool lastSignalCausal_ = true;
    double lastSignalSeconds_ = 2;
    quint64 signalReadRevision_ = 0;
    struct ProcessingWorker {
        std::mutex mutex;
        SignalProcessor processor;
    };
    std::shared_ptr<ProcessingWorker> processingWorker_ = std::make_shared<ProcessingWorker>();
};

} // namespace bf
