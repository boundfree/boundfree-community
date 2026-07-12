#pragma once

#include "controller/file_open_controller.h"
#include "controller/waveform_view_state.h"
#include "core/app_error.h"
#include "core/version.h"
#include "i18n/i18n_service.h"
#include "ui/waveform_widget.h"

#include <QAction>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QJsonObject>
#include <QStackedWidget>
#include <QToolBar>
#include <QVector>

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

private:
    void buildLanguageMenu();
    void buildBrowserControls();
    void refreshBrowserControls();
    void applyBrowserStateToWaveform();

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
    WaveformViewState viewState_;
    FileOpenResult currentFile_;
    bool hasCurrentFile_ = false;
    QString statusKey_ = "app.status.ready";
    QJsonObject statusParams_;
    AppError lastError_;
};

}  // namespace bf
