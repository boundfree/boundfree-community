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

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
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
#include <QToolBar>
#include <QVector>
#include <QVBoxLayout>
#include <QTableWidget>
#include <QTimer>

#include <functional>

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
    void setOpenFileHandler(std::function<void(const QString&)> handler);
    void setWindowRequestHandler(std::function<void(const QString&, double, double)> handler);
    void setBrowserWindowSecondsForBrowser(double seconds);
    void setExportMarkersHandler(std::function<SidecarResponse(const QString&, const QVector<SidecarEvent>&)> handler);
    void openPathForBrowser(const QString& path);
    void setProcessingViewStateForBrowser(const ProcessingViewState& state);
    ProcessingViewState processingViewState() const { return processingState_; }
    bool loadVideoForBrowser(const QString& path);
    QString videoStatusSummary() const;
    QString recordStatusSummary() const;
    QString exportStatusSummary() const;

private:
    void buildViewMenu();
    void buildThemeMenu();
    void applyTheme(bool dark);
    void refreshPersistentStatus();
    QString currentModeKey() const;
    void buildFileMenu();
    void buildLanguageMenu();
    void buildBrowserControls();
    void buildProcessingControls();
    void buildAnalysisDock();
    void chooseFile();
    void chooseVideo();
    void buildVideoDock();
    void buildRecordControls();
    void toggleRecording();
    void exportSegment();
    void syncVideoToPlayback();
    void updatePlaybackDuration();
    void requestWindow(double t0);
    void setLoading(bool loading);
    void refreshBrowserControls();
    void refreshProcessingControls();
    void applyBrowserStateToWaveform();
    void applyProcessingAndDisplay();
    void refreshAnalysisControls();
    void requestCurrentAnalysis();
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
    QStackedWidget* centralStack_ = nullptr;
    QWidget* emptyPage_ = nullptr;
    QLabel* centralLabel_ = nullptr;
    QPushButton* openButton_ = nullptr;
    WaveformWidget* waveformWidget_ = nullptr;
    QMenu* fileMenu_ = nullptr;
    QAction* openAction_ = nullptr;
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
    bool darkTheme_ = false;
    QActionGroup* languageGroup_ = nullptr;
    QVector<QAction*> languageActions_;
    QToolBar* browserToolbar_ = nullptr;
    QAction* previousWindowAction_ = nullptr;
    QAction* nextWindowAction_ = nullptr;
    QLabel* positionLabel_ = nullptr;
    QDoubleSpinBox* positionSpin_ = nullptr;
    QLabel* timeWindowLabel_ = nullptr;
    QDoubleSpinBox* timeWindowSpin_ = nullptr;
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
    CollapsiblePanel* analysisPanel_ = nullptr;
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

    bool refreshingBrowserControls_ = false;
    bool refreshingAnalysisControls_ = false;
    std::function<void(const QString&)> openFileHandler_;
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
    QTimer playbackTimer_;
    QElapsedTimer playbackElapsed_;
    bool refreshingMarkerControls_ = false;
    bool playbackPrefetchPending_ = false;
    QJsonObject emptyStateParams_;
    AppError lastError_;
};

}  // namespace bf
