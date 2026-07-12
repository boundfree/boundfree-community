#pragma once

#include "controller/file_open_controller.h"
#include "controller/waveform_view_state.h"
#include "core/app_error.h"
#include "core/version.h"
#include "i18n/i18n_service.h"
#include "ui/waveform_widget.h"

#include <QAction>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QJsonObject>
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
    void setWindowRequestHandler(std::function<void(const QString&, double, double)> handler);
    void setBrowserWindowSecondsForBrowser(double seconds);

private:
    void buildLanguageMenu();
    void buildBrowserControls();
    void refreshBrowserControls();
    void applyBrowserStateToWaveform();
    void rebuildChannelControls();
    QString channelCheckSummary() const;
    QString visibleDisplayChannelSummary() const;
    int channelCount() const;

    VersionInfo version_;
    I18nService* i18n_ = nullptr;
    QStackedWidget* centralStack_ = nullptr;
    QLabel* centralLabel_ = nullptr;
    WaveformWidget* waveformWidget_ = nullptr;
    QMenu* languageMenu_ = nullptr;
    QVector<QAction*> languageActions_;
    QToolBar* browserToolbar_ = nullptr;
    QDoubleSpinBox* timeWindowSpin_ = nullptr;
    QDoubleSpinBox* amplitudeSpin_ = nullptr;
    QSpinBox* firstChannelSpin_ = nullptr;
    QScrollArea* channelScroll_ = nullptr;
    QWidget* channelListWidget_ = nullptr;
    QVBoxLayout* channelListLayout_ = nullptr;
    QVector<QCheckBox*> channelChecks_;
    bool refreshingBrowserControls_ = false;
    std::function<void(const QString&, double, double)> windowRequestHandler_;
    WaveformViewState viewState_;
    FileOpenResult currentFile_;
    bool hasCurrentFile_ = false;
    QString statusKey_ = "app.status.ready";
    QJsonObject statusParams_;
    AppError lastError_;
};

}  // namespace bf
