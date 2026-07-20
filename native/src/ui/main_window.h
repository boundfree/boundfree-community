#pragma once

#include "controller/file_open_controller.h"
#include "controller/processing_view_state.h"
#include "controller/waveform_view_state.h"
#include "core/app_error.h"
#include "core/version.h"
#include "i18n/i18n_service.h"
#include "ui/waveform_widget.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QJsonObject>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSpinBox>
#include <QToolBar>
#include <QVector>
#include <QVBoxLayout>

#include <functional>

namespace bf {

class MainWindow : public QMainWindow {
public:
    MainWindow(const VersionInfo& version, I18nService* i18n, QWidget* parent = nullptr);

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
    void openPathForBrowser(const QString& path);
    void setProcessingViewStateForBrowser(const ProcessingViewState& state);
    ProcessingViewState processingViewState() const { return processingState_; }

private:
    void buildFileMenu();
    void buildLanguageMenu();
    void buildBrowserControls();
    void buildProcessingControls();
    void chooseFile();
    void requestWindow(double t0);
    void setLoading(bool loading);
    void refreshBrowserControls();
    void refreshProcessingControls();
    void applyBrowserStateToWaveform();
    void applyProcessingAndDisplay();
    void rebuildChannelControls();
    void onProcessingUiChanged();
    QString channelCheckSummary() const;
    QString visibleDisplayChannelSummary() const;
    int channelCount() const;

    VersionInfo version_;
    I18nService* i18n_ = nullptr;
    QStackedWidget* centralStack_ = nullptr;
    QWidget* emptyPage_ = nullptr;
    QLabel* centralLabel_ = nullptr;
    QPushButton* openButton_ = nullptr;
    WaveformWidget* waveformWidget_ = nullptr;
    QMenu* fileMenu_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* quitAction_ = nullptr;
    QMenu* languageMenu_ = nullptr;
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
    QScrollArea* channelScroll_ = nullptr;
    QWidget* channelListWidget_ = nullptr;
    QVBoxLayout* channelListLayout_ = nullptr;
    QVector<QCheckBox*> channelChecks_;

    QLabel* referenceLabel_ = nullptr;
    QComboBox* referenceCombo_ = nullptr;
    QCheckBox* notchEnableCheck_ = nullptr;
    QVector<QCheckBox*> notchBandChecks_;
    QVector<QDoubleSpinBox*> notchBandSpins_;
    QCheckBox* notchHarmonicsCheck_ = nullptr;
    QSpinBox* notchHarmonicsSpin_ = nullptr;
    QCheckBox* bandpassEnableCheck_ = nullptr;
    QDoubleSpinBox* bandpassLowSpin_ = nullptr;
    QDoubleSpinBox* bandpassHighSpin_ = nullptr;

    bool refreshingBrowserControls_ = false;
    bool refreshingProcessingControls_ = false;
    std::function<void(const QString&)> openFileHandler_;
    std::function<void(const QString&, double, double)> windowRequestHandler_;
    WaveformViewState viewState_;
    ProcessingViewState processingState_;
    FileOpenResult currentFile_;
    WaveformSlice rawWaveform_;
    bool hasCurrentFile_ = false;
    QString statusKey_ = "app.status.ready";
    QJsonObject statusParams_;
    QString emptyStateKey_ = "app.main.empty_state";
    QJsonObject emptyStateParams_;
    AppError lastError_;
};

}  // namespace bf
