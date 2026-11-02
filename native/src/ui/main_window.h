#pragma once

#include "controller/file_open_controller.h"
#include "controller/processing_view_state.h"
#include "controller/marker_store.h"
#include "controller/playback_state.h"
#include "controller/analysis_service.h"
#include "controller/waveform_view_state.h"
#include "controller/export_controller.h"
#include "controller/record_controller.h"
#include "core/app_error.h"
#include "core/version.h"
#include "i18n/i18n_service.h"
#include "ui/eeg_playback_panel.h"
#include "ui/eeg_processing_panel.h"
#include "ui/waveform_widget.h"
#include "ui/analysis_widgets.h"
#include "ui/video_panel.h"
#include "ui/collapsible_panel.h"
#include "ui/channel_bandpass_controls.h"
#include "ui/eeg_separation_workspace.h"
#include "ui/device_workspace.h"
#include "controller/live_ica_controller.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QJsonObject>
#include <QPushButton>
#include <QSlider>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSpinBox>
#include <QTabWidget>
#include <QTabBar>
#include "ui/control_bar.h"
#include <QSplitter>
#include <QVector>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QTimer>

#include <functional>
#include <array>

namespace bf {

class MainWindow : public QMainWindow {
public:
    MainWindow(const VersionInfo& version, I18nService* i18n, const QString& repoRoot = {}, QWidget* parent = nullptr);

    void retranslateUi();
    void setSidecarConnecting();
    void setSidecarConnected();
    void setSidecarFailed(const AppError& error);
    void showFileOpenLoading(const QString& path);
    void showFileOpenResult(const FileOpenResult& result);
    void showFileOpenError(const AppError& error);
    void setBrowserViewState(const WaveformViewState& state);
    QString browserStatusText() const;
    void setFirstVisibleChannelForBrowser(int channel);
    void setChannelVisibleForBrowser(int channel, bool visible);
    QString browserControlSummary() const;
    QString processingSummary() const;
    void setCloseFileHandler(std::function<void()> handler) { closeFileHandler_ = std::move(handler); }
    void setOpenFileHandler(std::function<void(const QString&)> handler);
    void setOpenCaptureHandler(std::function<void(const QString&, const QString&)> handler) { openCaptureHandler_ = std::move(handler); }
    void setLogLocationHandler(std::function<bool(const QString&)> handler) { logLocationHandler_ = std::move(handler); }
    void setWindowRequestHandler(std::function<void(const QString&, double, double)> handler);
    void setBrowserWindowSecondsForBrowser(double seconds);
    void setExportMarkersHandler(std::function<SidecarResponse(const QString&, const QVector<SidecarEvent>&)> handler);
    void openPathForBrowser(const QString& path);
    void setProcessingViewStateForBrowser(const ProcessingViewState& state);
    const WaveformSlice& signalWindow(bool original=false) const {return original?activeRawSignal():activeSignal();}
    ProcessingViewState processingViewState() const { return processingState_; }
    bool loadVideoForBrowser(const QString& path);
    QString videoStatusSummary() const;
    QString recordStatusSummary() const;
    QString exportStatusSummary() const;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildFixedWorkspace();
    void updateSignalWorkspaceLayout();
    QSplitter* signalWorkspaceSplitter_ = nullptr;
    QWidget* signalPage_ = nullptr;
    QSplitter* signalAnalysisPane_ = nullptr;
    QTabWidget* compactSignalTabs_ = nullptr;
    bool compactSignalLayout_ = false;
    void switchWorkspace(int index);
    void resetSeparationForBrowserSource();
    void buildViewMenu();
    void buildThemeMenu();
    void applyTheme(bool dark);
    void refreshPersistentStatus();
    void refreshDeviceStatus();
    void openDeviceConnections();
    void showDeviceWaveform();
    void refreshAnalysisInputSummary();
    bool hasActiveSignal() const;
    const WaveformSlice& activeSignal() const;
    const WaveformSlice& activeRawSignal() const;
    void refreshLiveSignal(double start,double end,bool frozen,int channel,double seconds);
    void displayLiveSignal();
    void prepareSeparationSnapshot();
    void prepareDeviceSeparationSnapshot(const QString& stream);
    void writeSeparationSnapshot(const WaveformSlice& raw, const QString& name = {});
    LiveIcaController* liveIcaController_ = nullptr;
    QTimer* separationCollectTimer_ = nullptr;
    QElapsedTimer separationCollectClock_;
    QString separationCollectStream_, separationCollectSource_;
    bool separationCollecting_ = false;
    ExportController* separationSnapshotExporter_ = nullptr;
    quint64 separationSnapshotRequest_ = 0;
    quint64 separationSnapshotExportRequest_ = 0;
    QString separationSnapshotDeviceSource_;
    QString snapshotDirectory_;
    bool preparingSeparation_ = false;
    QString analysisUnitLabel(AnalysisKind,const QString&,bool) const;
    QString processingDescription() const;
    WaveformSlice selectedWaveform();
    QString processingError_;
    QString currentModeKey() const;
    void buildFileMenu();
    void buildDeviceMenu();
    void buildLanguageMenu();
    void buildBrowserControls();
    void buildProcessingControls();
    void buildAnalysisDock();
    void chooseFile();
    void refreshInputActions();
    void chooseCapture();
    bool isCaptureReplay() const;
    void refreshSignalTools();
    void chooseVideo();
    void buildVideoDock();
    void buildRecordControls();
    void toggleRecording();
    void exportSegment();
    void syncVideoToPlayback();
    void updatePlaybackDuration();
    void requestWindow(double t0);
    void cancelScrubPreview();
    void previewScrubPosition(double position);
    void requestScrubPreview();
    double viewportStart(double position) const;
    bool cacheCoversPosition(double position) const;
    void updatePlaybackViewport();
    void requestPlaybackPrefetch();
    void setLoading(bool loading);
    void refreshBrowserControls();
    void refreshProcessingControls();
    void applyBrowserStateToWaveform();
    void applyProcessingAndDisplay();
    void refreshAnalysisControls();
    void requestCurrentAnalysis(bool background = false);
    void invalidateAnalysis();
    void refreshAnalysisPage(int page);
    void renderAnalysisResult(int page);
    void clearAnalysisPlot(int page);
    void handleAnalysisCompleted(int generation, const AnalysisResult& result);
    void handleAnalysisFailed(int generation, const QString& code, const QString& detail);
    void rebuildChannelControls();
    void buildPlaybackControls();
    void buildMarkerDock();
    void refreshPlaybackControls();
    void refreshMarkerControls();
    void togglePlayback();
    void advancePlayback();
    void addMarkerAt(double time);
    void deleteSelectedMarker();
    void clearMarkers();
    void exportMarkers();
    void onProcessingUiChanged();
    // Write the current global bandpass as a per-channel override on every
    // currently visible channel.
    void applyBandpassToSelectedChannels();
    // Remove all per-channel bandpass overrides.
    void clearPerChannelBandpassOverrides();
    QString channelCheckSummary() const;
    QString visibleDisplayChannelSummary() const;
    int channelCount() const;

    VersionInfo version_;
    QString repoRoot_;
    I18nService* i18n_ = nullptr;
    QStackedWidget* workspaceStack_ = nullptr;
    QTabBar* workspaceNavigation_ = nullptr;
    EegSeparationController* separationController_ = nullptr;
    EegSeparationWorkspace* separationWorkspace_ = nullptr;
    AcquisitionController* acquisitionController_ = nullptr;
    DeviceWorkspace* deviceWorkspace_ = nullptr;
    QDialog* deviceDialog_ = nullptr;
    QWidget* deviceLiveControls_ = nullptr;
    bool deviceWaveformActive_ = false;
    QPushButton* deviceStatusButton_ = nullptr;
    bool deviceShutdownPending_ = false;
    bool deviceShutdownApproved_ = false;
    QAction* separationAction_ = nullptr;
    bool separationResetPending_ = false;
    QStackedWidget* centralStack_ = nullptr;
    QWidget* emptyPage_ = nullptr;
    QLabel* centralLabel_ = nullptr;
    QPushButton* openButton_ = nullptr;
    WaveformWidget* waveformWidget_ = nullptr;
    QMenu* fileMenu_ = nullptr;
    QMenu* deviceMenu_ = nullptr;
    QAction* deviceManageAction_ = nullptr;
    QAction* deviceOpenLogsAction_ = nullptr;
    std::function<bool(const QString&)> logLocationHandler_;
    QAction* openAction_ = nullptr;
    QAction* openTeachingAction_ = nullptr;
    bool fileMenuLoading_ = false;
    QAction* openVideoAction_ = nullptr;
    QAction* quitAction_ = nullptr;
    QMenu* languageMenu_ = nullptr;
    QMenu* viewMenu_ = nullptr;
    QMenu* themeMenu_ = nullptr;
    QActionGroup* themeGroup_ = nullptr;
    QAction* lightThemeAction_ = nullptr;
    QAction* darkThemeAction_ = nullptr;
    QAction* toggleChannelsAction_ = nullptr;
    QAction* toggleMarkersAction_ = nullptr;
    QAction* toggleAnalysisAction_ = nullptr;
    QAction* toggleVideoAction_ = nullptr;
    QLabel* statusFileLabel_ = nullptr;
    QLabel* statusModeLabel_ = nullptr;
    QLabel* recordingStatusLabel_ = nullptr;
    bool recordingStatusVisible_ = false;
    bool darkTheme_ = false;
    QActionGroup* languageGroup_ = nullptr;
    QVector<QAction*> languageActions_;
    ControlBar* playbackToolbar_ = nullptr;
    QWidget* filterToolbar_ = nullptr;
    QWidget* playbackBar_ = nullptr;
    QWidget* progressRow_ = nullptr;
    CollapsiblePanel* filterPanel_ = nullptr;
    CollapsiblePanel* channelFilterPanel_ = nullptr;
    QLabel* waveformTitle_ = nullptr;
    QTabWidget* parameterTabs_ = nullptr;
    QSplitter* signalSplitter_ = nullptr;
    ControlBar* browserToolbar_ = nullptr;
    QLabel* waveformDataLabel_ = nullptr;
    QComboBox* waveformDataCombo_ = nullptr;
    bool waveformProcessed_ = true;
    QAction* previousWindowAction_ = nullptr;
    QAction* nextWindowAction_ = nullptr;
    QLabel* positionLabel_ = nullptr;
    QDoubleSpinBox* positionSpin_ = nullptr;
    QLabel* timeWindowLabel_ = nullptr;
    QDoubleSpinBox* timeWindowSpin_ = nullptr;
    QCheckBox* autoAmplitudeCheck_ = nullptr;
    QLabel* amplitudeLabel_ = nullptr;
    QDoubleSpinBox* amplitudeSpin_ = nullptr;
    QLabel* firstChannelLabel_ = nullptr;
    QSpinBox* firstChannelSpin_ = nullptr;
    QDockWidget* channelDock_ = nullptr;
    CollapsiblePanel* channelPanel_ = nullptr;
    QAction* playAction_ = nullptr;
    EegPlaybackPanel* playbackPanel_ = nullptr;
    QScrollArea* channelScroll_ = nullptr;
    QWidget* channelListWidget_ = nullptr;
    QVBoxLayout* channelListLayout_ = nullptr;
    QVector<QCheckBox*> channelChecks_;
    QStringList channelControlNames_;
    ChannelBandpassControls* channelBandpassControls_ = nullptr;
    QDockWidget* markerDock_ = nullptr;
    QTableWidget* markerTable_ = nullptr;
    CollapsiblePanel* markerPanel_ = nullptr;
    QPushButton* deleteMarkerButton_ = nullptr;
    QPushButton* clearMarkersButton_ = nullptr;
    QPushButton* exportMarkersButton_ = nullptr;
    QAction* markerModeAction_ = nullptr;
    QDockWidget* videoDock_ = nullptr;
    QAction* recordAction_ = nullptr;
    QAction* exportSegmentAction_ = nullptr;
    RecordController* recordController_ = nullptr;
    ExportController* exportController_ = nullptr;
    RecordSummary lastRecordSummary_;
    SidecarExportResult lastExportResult_;
    bool exportRunning_ = false;
    double recordSourceStart_ = 0.0;
    QElapsedTimer recordElapsed_;
    QTimer recordUiTimer_;
    QString recordOutputDir_;
    bool recordBlinkVisible_ = false;
    VideoPanel* videoPanel_ = nullptr;

    EegProcessingPanel* processingPanel_ = nullptr;

    QDockWidget* analysisDock_ = nullptr;
    QTabWidget* analysisTabs_ = nullptr;
    QLabel* analysisHeading_ = nullptr;
    QLabel* analysisInputSummary_ = nullptr;
    QLabel* coherenceALabel_ = nullptr;
    QLabel* coherenceBLabel_ = nullptr;
    QLabel* coherenceFmaxLabel_ = nullptr;
    QComboBox* psdChannelCombo_ = nullptr;
    QComboBox* psdModeCombo_ = nullptr;
    QCheckBox* psdDbCheck_ = nullptr;
    QSpinBox* psdFmaxSpin_ = nullptr;
    SpectrumWidget* spectrumWidget_ = nullptr;
    QLabel* psdBandSummaryLabel_ = nullptr;
    QComboBox* stftChannelCombo_ = nullptr;
    QComboBox* stftResolutionCombo_ = nullptr;
    QSpinBox* stftFmaxSpin_ = nullptr;
    SpectrogramWidget* spectrogramWidget_ = nullptr;
    QComboBox* trendChannelCombo_ = nullptr;
    QDoubleSpinBox* trendEpochSpin_ = nullptr;
    BandTrendWidget* trendWidget_ = nullptr;
    QComboBox* coherenceACombo_ = nullptr;
    QComboBox* coherenceBCombo_ = nullptr;
    QSpinBox* coherenceFmaxSpin_ = nullptr;
    QLabel* coherenceSummaryLabel_ = nullptr;
    SpectrumWidget* coherenceWidget_ = nullptr;
    AnalysisService* analysisService_ = nullptr;
    int analysisGeneration_ = 0;
    int requestedAnalysisPage_ = -1;
    enum class AnalysisDisplayState { Idle, Pending, Ready, Empty, Failed };
    struct AnalysisPageState {
        AnalysisDisplayState state = AnalysisDisplayState::Idle;
        AnalysisResult result;
        AnalysisKind kind = AnalysisKind::Psd;
        QString channel;
        QString errorCode;
        QString unit = "uV";
        bool partialWindow = false;
        double start = 0.0;
        double end = 0.0;
        double fmax = 100.0;
        bool asDb = false;
    };
    std::array<AnalysisPageState, 4> analysisPages_;
    AnalysisPageState pendingAnalysis_;
    bool analysisRequestPending_ = false;
    std::array<QLabel*, 4> analysisTitleLabels_{};
    std::array<QLabel*, 4> analysisStatusLabels_{};
    std::array<QLabel*, 4> analysisLegendLabels_{};
    std::array<QLabel*, 4> analysisSummaryLabels_{};

    bool refreshingBrowserControls_ = false;
    bool refreshingAnalysisControls_ = false;
    std::function<void(const QString&)> openFileHandler_;
    std::function<void()> closeFileHandler_;
    std::function<void(const QString&, const QString&)> openCaptureHandler_;
    QWidget* captureBar_ = nullptr;
    QComboBox* captureStreamCombo_ = nullptr;
    QLabel *captureStreamLabel_ = nullptr, *captureSummary_ = nullptr;
    QAction* openCaptureAction_ = nullptr;
    WaveformSlice liveRawSignal_, liveProcessedSignal_;
    QComboBox* processingModeCombo_ = nullptr;
    QString liveStreamKey_;
    bool liveFrozen_ = false;
    int liveChannel_ = -1;
    std::pair<double,double> liveRange_{0,2};
    QElapsedTimer liveReadElapsed_;
    std::function<void(const QString&, double, double)> windowRequestHandler_;
    WaveformViewState viewState_;
    ProcessingViewState processingState_;
    FileOpenResult currentFile_;
    std::function<SidecarResponse(const QString&, const QVector<SidecarEvent>&)> exportMarkersHandler_;
    WaveformSlice rawWaveform_;
    bool hasCurrentFile_ = false;
    QString statusKey_ = "app.status.ready";
    QJsonObject statusParams_;
    QString emptyStateKey_ = "app.main.empty_state";
    PlaybackState playback_;
    double signalDurationSeconds_ = 0.0;
    MarkerStore markerStore_;
    QString liveMarkerSource_;
    QTimer playbackTimer_;
    QElapsedTimer playbackElapsed_;
    bool refreshingMarkerControls_ = false;
    bool playbackPrefetchPending_ = false;
    double playbackPrefetchEnd_ = 0.0;
    bool windowRequestPending_ = false;
    bool waitingForPlaybackData_ = false;
    bool openingFile_ = false;
    bool playbackViewportActive_ = false;
    QTimer scrubPreviewTimer_;
    double scrubPreviewPosition_ = 0.0;
    double scrubDisplayedPosition_ = 0.0;
    bool scrubPreviewDisplayed_ = false;
    bool scrubRequestPending_ = false;
    QElapsedTimer analysisElapsed_;
    QJsonObject emptyStateParams_;
    AppError lastError_;
};

}  // namespace bf
