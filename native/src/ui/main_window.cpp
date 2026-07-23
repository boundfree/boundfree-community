#include "ui/main_window.h"

#include "controller/processing_view_state.h"
#include "render/waveform_slice.h"

#include <QActionGroup>
#include <QPalette>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeySequence>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QMenuBar>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTime>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace bf {

MainWindow::MainWindow(const VersionInfo& version, I18nService* i18n, const QString& repoRoot, QWidget* parent)
    : QMainWindow(parent), version_(version), repoRoot_(repoRoot), i18n_(i18n) {
    centralStack_ = new QStackedWidget(this);
    emptyPage_ = new QWidget(centralStack_);
    auto* emptyLayout = new QVBoxLayout(emptyPage_);
    emptyLayout->setAlignment(Qt::AlignCenter);
    centralLabel_ = new QLabel(emptyPage_);
    centralLabel_->setAlignment(Qt::AlignCenter);
    openButton_ = new QPushButton(emptyPage_);
    openButton_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    openButton_->setMinimumWidth(140);
    emptyLayout->addWidget(centralLabel_, 0, Qt::AlignCenter);
    emptyLayout->addWidget(openButton_, 0, Qt::AlignCenter);
    waveformWidget_ = new WaveformWidget(this);
    centralStack_->addWidget(emptyPage_);
    centralStack_->addWidget(waveformWidget_);
    setCentralWidget(centralStack_);
    buildFileMenu();
    buildLanguageMenu();

    buildBrowserControls();
    buildProcessingControls();
    buildAnalysisDock();
    buildPlaybackControls();
    buildMarkerDock();
    buildVideoDock();
    buildRecordControls();
    buildViewMenu();
    buildThemeMenu();
    recordUiTimer_.setInterval(500);
    connect(&recordUiTimer_, &QTimer::timeout, this, [this]() {
        if (recordController_ == nullptr || !recordController_->isActive()) return;
        recordBlinkVisible_ = !recordBlinkVisible_;
        recordAction_->setText((recordBlinkVisible_ ? QStringLiteral("● ") : QString()) + i18n_->text("app.record.stop"));
        const qint64 freeBytes = QStorageInfo(recordOutputDir_).bytesAvailable();
        const QString elapsed = QTime::fromMSecsSinceStartOfDay(static_cast<int>(recordElapsed_.elapsed() % 86400000)).toString("HH:mm:ss");
        statusBar()->showMessage(i18n_->text(
            freeBytes >= 0 && freeBytes < 500LL * 1024LL * 1024LL ? "app.record.low_disk" : "app.record.status",
            {{"elapsed", elapsed}, {"samples", lastRecordSummary_.samplesWritten}, {"dropped", lastRecordSummary_.sourceDroppedChunks + lastRecordSummary_.droppedChunks}}));
    });
    playbackTimer_.setInterval(25);
    connect(&playbackTimer_, &QTimer::timeout, this, &MainWindow::advancePlayback);
    connect(waveformWidget_, &WaveformWidget::markerRequested, this, &MainWindow::addMarkerAt);
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::chooseFile);
    statusFileLabel_ = new QLabel(this);
    statusModeLabel_ = new QLabel(this);
    statusFileLabel_->setMinimumWidth(40);
    statusModeLabel_->setMinimumWidth(40);
    statusBar()->addPermanentWidget(statusModeLabel_);
    statusBar()->addPermanentWidget(statusFileLabel_);
    resize(900, 560);
    retranslateUi();
}

void MainWindow::buildFileMenu() {
    fileMenu_ = menuBar()->addMenu(QString());
    openAction_ = fileMenu_->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QString());
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::chooseFile);
    openVideoAction_ = fileMenu_->addAction(QString());
    openVideoAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(openVideoAction_, &QAction::triggered, this, &MainWindow::chooseVideo);
    fileMenu_->addSeparator();
    quitAction_ = fileMenu_->addAction(style()->standardIcon(QStyle::SP_DialogCloseButton), QString());
    quitAction_->setShortcut(QKeySequence::Quit);
    connect(quitAction_, &QAction::triggered, this, &QWidget::close);
}

void MainWindow::buildLanguageMenu() {
    languageMenu_ = menuBar()->addMenu(QString());
    const QStringList locales = i18n_->supportedLocales();
    for (const QString& locale : locales) {
        auto* action = languageMenu_->addAction(QString());
        action->setCheckable(true);
        action->setData(locale);
        QObject::connect(action, &QAction::triggered, this, [this, locale]() {
            QString error;
            if (i18n_->loadLocale(locale, &error)) {
                retranslateUi();
            }
        });
        languageActions_.append(action);
    }
}

void MainWindow::buildViewMenu() {
    viewMenu_ = menuBar()->addMenu(QString());
    toggleChannelsAction_ = viewMenu_->addAction(QString());
    toggleMarkersAction_ = viewMenu_->addAction(QString());
    toggleAnalysisAction_ = viewMenu_->addAction(QString());
    toggleVideoAction_ = viewMenu_->addAction(QString());
    auto wireToggle = [this](QAction* action, QDockWidget* dock) {
        action->setCheckable(true);
        action->setChecked(dock->isVisible());
        action->setEnabled(true);
        connect(action, &QAction::triggered, dock, &QDockWidget::setVisible);
        connect(dock, &QDockWidget::visibilityChanged, action, &QAction::setChecked);
    };
    wireToggle(toggleChannelsAction_, channelDock_);
    wireToggle(toggleMarkersAction_, markerDock_);
    wireToggle(toggleAnalysisAction_, analysisDock_);
    wireToggle(toggleVideoAction_, videoDock_);
}

void MainWindow::buildThemeMenu() {
    themeMenu_ = menuBar()->addMenu(QString());
    themeGroup_ = new QActionGroup(this);
    themeGroup_->setExclusive(true);
    lightThemeAction_ = themeMenu_->addAction(QString());
    lightThemeAction_->setCheckable(true);
    darkThemeAction_ = themeMenu_->addAction(QString());
    darkThemeAction_->setCheckable(true);
    themeGroup_->addAction(lightThemeAction_);
    themeGroup_->addAction(darkThemeAction_);
    darkThemeAction_->setChecked(true);
    connect(lightThemeAction_, &QAction::triggered, this, [this]() { applyTheme(false); });
    connect(darkThemeAction_, &QAction::triggered, this, [this]() { applyTheme(true); });
    applyTheme(darkTheme_);
}

void MainWindow::applyTheme(bool dark) {
    darkTheme_ = dark;
    // Tsinghua purple accent (#660874) used across both themes.
    const QColor accent("#660874");
    QPalette palette;
    if (dark) {
        palette.setColor(QPalette::Window, QColor("#111827"));
        palette.setColor(QPalette::Base, QColor("#0B1220"));
        palette.setColor(QPalette::AlternateBase, QColor("#1B2433"));
        palette.setColor(QPalette::WindowText, QColor("#E2E8F0"));
        palette.setColor(QPalette::Text, QColor("#E2E8F0"));
        palette.setColor(QPalette::Button, QColor("#1F2937"));
        palette.setColor(QPalette::ButtonText, QColor("#E2E8F0"));
        palette.setColor(QPalette::Highlight, accent);
        palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
        palette.setColor(QPalette::ToolTipBase, QColor("#1F2937"));
        palette.setColor(QPalette::ToolTipText, QColor("#E2E8F0"));
    } else {
        palette.setColor(QPalette::Window, QColor("#FFFFFF"));
        palette.setColor(QPalette::Base, QColor("#F8FAFC"));
        palette.setColor(QPalette::AlternateBase, QColor("#EEF2F7"));
        palette.setColor(QPalette::WindowText, QColor("#0F172A"));
        palette.setColor(QPalette::Text, QColor("#0F172A"));
        palette.setColor(QPalette::Button, QColor("#E5E7EB"));
        palette.setColor(QPalette::ButtonText, QColor("#0F172A"));
        palette.setColor(QPalette::Highlight, accent);
        palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
        palette.setColor(QPalette::ToolTipBase, QColor("#E5E7EB"));
        palette.setColor(QPalette::ToolTipText, QColor("#0F172A"));
    }
    setPalette(palette);
    for (QWidget* child : findChildren<QWidget*>()) {
        child->setPalette(palette);
    }
    if (waveformWidget_ != nullptr) {
        waveformWidget_->setPalette(palette);
        waveformWidget_->setDarkTheme(darkTheme_);
    }
    if (spectrumWidget_ != nullptr) spectrumWidget_->setDarkTheme(darkTheme_);
    if (spectrogramWidget_ != nullptr) spectrogramWidget_->setDarkTheme(darkTheme_);
    if (trendWidget_ != nullptr) trendWidget_->setDarkTheme(darkTheme_);
    if (coherenceWidget_ != nullptr) coherenceWidget_->setDarkTheme(darkTheme_);
    update();
}

void MainWindow::refreshPersistentStatus() {
    if (statusFileLabel_ == nullptr || statusModeLabel_ == nullptr) {
        return;
    }
    statusFileLabel_->setText(hasCurrentFile_ ? QFileInfo(currentFile_.session.path).fileName() : QString());
    statusModeLabel_->setText(i18n_->text(currentModeKey()));
}

QString MainWindow::currentModeKey() const {
    if (recordController_ != nullptr && recordController_->isActive()) {
        return QStringLiteral("app.status.mode.recording");
    }
    if (playback_.isPlaying()) {
        return QStringLiteral("app.status.mode.playback");
    }
    return QStringLiteral("app.status.mode.idle");
}

void MainWindow::buildBrowserControls() {
    browserToolbar_ = addToolBar("Browser");
    browserToolbar_->setMovable(false);

    previousWindowAction_ = browserToolbar_->addAction(style()->standardIcon(QStyle::SP_ArrowBack), QString());
    connect(previousWindowAction_, &QAction::triggered, this, [this]() {
        requestWindow(currentFile_.waveform.t0 - viewState_.windowSeconds);
    });
    nextWindowAction_ = browserToolbar_->addAction(style()->standardIcon(QStyle::SP_ArrowForward), QString());
    connect(nextWindowAction_, &QAction::triggered, this, [this]() {
        requestWindow(currentFile_.waveform.t0 + viewState_.windowSeconds);
    });

    positionLabel_ = new QLabel(this);
    positionSpin_ = new QDoubleSpinBox(this);
    positionSpin_->setRange(0.0, 0.0);
    positionSpin_->setDecimals(3);
    positionSpin_->setSingleStep(1.0);
    positionSpin_->setSuffix(" s");
    positionSpin_->setMinimumWidth(100);
    playbackProgress_ = new QSlider(Qt::Horizontal, this);
    playbackProgress_->setRange(0, 10000);
    playbackProgress_->setMinimumWidth(160);
    browserToolbar_->addSeparator();
    browserToolbar_->addWidget(positionLabel_);
    browserToolbar_->addWidget(positionSpin_);
    browserToolbar_->addWidget(playbackProgress_);
    connect(positionSpin_, &QDoubleSpinBox::editingFinished, this, [this]() {
        if (!refreshingBrowserControls_) {
            playback_.seek(positionSpin_->value());
            if (playback_.position() < signalDurationSeconds_) {
                requestWindow(playback_.position());
            }
            syncVideoToPlayback();
            refreshPlaybackControls();
        }
    });

    connect(playbackProgress_, &QSlider::sliderReleased, this, [this]() {
        if (!hasCurrentFile_) {
            return;
        }
        const double position = playback_.duration() * playbackProgress_->value() / 10000.0;
        playback_.seek(position);
        if (position < signalDurationSeconds_) {
            requestWindow(position);
        }
        syncVideoToPlayback();
        refreshPlaybackControls();
    });
    timeWindowLabel_ = new QLabel(this);
    timeWindowSpin_ = new QDoubleSpinBox(this);
    timeWindowSpin_->setRange(1.0, 120.0);
    timeWindowSpin_->setDecimals(0);
    timeWindowSpin_->setValue(viewState_.windowSeconds);
    timeWindowSpin_->setSuffix(" s");

    amplitudeLabel_ = new QLabel(this);
    amplitudeSpin_ = new QDoubleSpinBox(this);
    amplitudeSpin_->setRange(50.0, 5000.0);
    amplitudeSpin_->setDecimals(0);
    amplitudeSpin_->setValue(viewState_.amplitudeUv);
    amplitudeSpin_->setSuffix(" uV");

    firstChannelLabel_ = new QLabel(this);
    firstChannelSpin_ = new QSpinBox(this);
    firstChannelSpin_->setRange(0, 0);
    firstChannelSpin_->setValue(viewState_.firstVisibleChannel);

    browserToolbar_->addSeparator();
    browserToolbar_->addWidget(timeWindowLabel_);
    browserToolbar_->addWidget(timeWindowSpin_);
    browserToolbar_->addSeparator();
    browserToolbar_->addWidget(amplitudeLabel_);
    browserToolbar_->addWidget(amplitudeSpin_);
    browserToolbar_->addSeparator();
    browserToolbar_->addWidget(firstChannelLabel_);
    browserToolbar_->addWidget(firstChannelSpin_);

    channelDock_ = new QDockWidget(this);
    channelDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    channelScroll_ = new QScrollArea(channelDock_);
    channelScroll_->setWidgetResizable(true);
    channelListWidget_ = new QWidget(channelScroll_);
    channelListLayout_ = new QVBoxLayout(channelListWidget_);
    channelListLayout_->setContentsMargins(4, 4, 4, 4);
    channelListLayout_->setSpacing(2);
    channelScroll_->setWidget(channelListWidget_);
    channelDock_->setWidget(channelScroll_);
    addDockWidget(Qt::LeftDockWidgetArea, channelDock_);
    channelDock_->hide();
    browserToolbar_->hide();

    connect(timeWindowSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (refreshingBrowserControls_) {
            return;
        }
        viewState_.setWindowSeconds(value);
        applyBrowserStateToWaveform();
        if (hasCurrentFile_ && windowRequestHandler_) {
            requestWindow(currentFile_.waveform.t0);
        }
    });
    connect(amplitudeSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (refreshingBrowserControls_) {
            return;
        }
        viewState_.setAmplitudeUv(value);
        applyBrowserStateToWaveform();
    });
    connect(firstChannelSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (refreshingBrowserControls_) {
            return;
        }
        viewState_.setFirstVisibleChannel(value);
        applyBrowserStateToWaveform();
    });
}

void MainWindow::buildPlaybackControls() {
    playAction_ = browserToolbar_->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QString());
    connect(playAction_, &QAction::triggered, this, &MainWindow::togglePlayback);
    speedLabel_ = new QLabel(this);
    speedCombo_ = new QComboBox(this);
    for (double speed : PlaybackState::kSpeeds) {
        speedCombo_->addItem(QString::number(speed) + "x", speed);
    }
    speedCombo_->setCurrentIndex(2);
    connect(speedCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        playback_.setSpeed(speedCombo_->itemData(index).toDouble());
        if (videoPanel_ != nullptr) {
            videoPanel_->setRate(playback_.speed());
            syncVideoToPlayback();
        }
    });
    browserToolbar_->addWidget(speedLabel_);
    browserToolbar_->addWidget(speedCombo_);
    markerModeAction_ = browserToolbar_->addAction(QString());
    markerModeAction_->setCheckable(true);
    connect(markerModeAction_, &QAction::toggled, this, [this](bool enabled) { waveformWidget_->setMarkerModeEnabled(enabled); });
}

void MainWindow::buildRecordControls() {
    const QString root = repoRoot_.isEmpty() ? QStringLiteral(BF_REPO_ROOT) : repoRoot_;
    recordController_ = new RecordController(root, version_, this);
    exportController_ = new ExportController(root, version_, this);
    recordAction_ = browserToolbar_->addAction(QString());
    exportSegmentAction_ = browserToolbar_->addAction(QString());
    connect(recordAction_, &QAction::triggered, this, &MainWindow::toggleRecording);
    connect(exportSegmentAction_, &QAction::triggered, this, &MainWindow::exportSegment);
    connect(recordController_, &RecordController::started, this, [this](const RecordSummary& summary) {
        lastRecordSummary_ = summary;
        recordElapsed_.restart();
        recordBlinkVisible_ = true;
        recordUiTimer_.start();
        recordAction_->setText(i18n_->text("app.record.stop"));
        statusBar()->showMessage(i18n_->text("app.record.started"));
        refreshPersistentStatus();
    });
    connect(recordController_, &RecordController::progress, this, [this](const RecordSummary& summary) {
        lastRecordSummary_ = summary;
    });
    connect(recordController_, &RecordController::stopped, this, [this](const RecordSummary& summary) {
        lastRecordSummary_ = summary;
        recordAction_->setText(i18n_->text("app.record.start"));
        statusBar()->showMessage(i18n_->text("app.record.completed", {{"samples", summary.samplesWritten}}), 5000);
        recordUiTimer_.stop();
        recordBlinkVisible_ = false;
        refreshPersistentStatus();
    });
    connect(recordController_, &RecordController::failed, this, [this](const AppError& error) {
        recordAction_->setText(i18n_->text("app.record.start"));
        recordUiTimer_.stop();
        recordBlinkVisible_ = false;
        refreshPersistentStatus();
        statusBar()->showMessage(i18n_->text(QString("error.%1").arg(error.code), error.params), 5000);
    });
    connect(exportController_, &ExportController::completed, this, [this](const SidecarExportResult& result) {
        exportRunning_ = false;
        lastExportResult_ = result;
        exportSegmentAction_->setEnabled(true);
        statusBar()->showMessage(i18n_->text("app.export.completed", {{"path", result.path}}), 5000);
    });
    connect(exportController_, &ExportController::failed, this, [this](const AppError& error) {
        exportRunning_ = false;
        exportSegmentAction_->setEnabled(true);
        statusBar()->showMessage(i18n_->text(QString("error.%1").arg(error.code), error.params), 5000);
    });
}

void MainWindow::toggleRecording() {
    if (recordController_->isActive()) {
        recordController_->stop();
        recordAction_->setEnabled(false);
        QTimer::singleShot(250, this, [this]() { recordAction_->setEnabled(true); });
        return;
    }
    if (!hasCurrentFile_) {
        statusBar()->showMessage(i18n_->text("app.record.no_file"), 3000);
        return;
    }
    bool accepted = false;
    const QString animalId = QInputDialog::getText(this, i18n_->text("app.record.metadata"), i18n_->text("app.record.animal_id"), QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    const QString expType = QInputDialog::getText(this, i18n_->text("app.record.metadata"), i18n_->text("app.record.exp_type"), QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    const QString notes = QInputDialog::getMultiLineText(this, i18n_->text("app.record.metadata"), i18n_->text("app.record.notes"), {}, &accepted);
    if (!accepted) return;
    const QString outputDir = QFileDialog::getExistingDirectory(this, i18n_->text("app.record.output_dir"));
    if (outputDir.isEmpty()) return;
    const QJsonObject metadata{{"animal_id", animalId}, {"exp_type", expType}, {"notes", notes}};
    recordOutputDir_ = outputDir;
    recordSourceStart_ = std::min(playback_.position(), std::max(0.0, currentFile_.session.duration - 1.0 / currentFile_.session.fs));
    if (!recordController_->startFromFile(currentFile_.session.path, currentFile_.session.fs, currentFile_.session.chNames, currentFile_.session.duration, recordSourceStart_, outputDir, metadata)) {
        statusBar()->showMessage(i18n_->text("error.record.invalid_state", {{"state", "busy"}}), 3000);
    }
}

void MainWindow::exportSegment() {
    if (!hasCurrentFile_ || exportController_->isBusy()) {
        statusBar()->showMessage(i18n_->text(exportController_->isBusy() ? "error.export.busy" : "app.record.no_file"), 3000);
        return;
    }
    bool accepted = false;
    const double t0 = QInputDialog::getDouble(this, i18n_->text("app.export.title"), i18n_->text("app.export.start"), playback_.position(), 0.0, signalDurationSeconds_, 3, &accepted);
    if (!accepted) return;
    const double t1 = QInputDialog::getDouble(this, i18n_->text("app.export.title"), i18n_->text("app.export.end"), std::min(signalDurationSeconds_, t0 + viewState_.windowSeconds), 0.0, signalDurationSeconds_, 3, &accepted);
    if (!accepted) return;
    const QString outputPath = QFileDialog::getSaveFileName(this, i18n_->text("app.export.title"), {}, i18n_->text("app.export.filter"));
    if (outputPath.isEmpty()) return;
    QVector<SidecarEvent> events;
    for (const Marker& marker : markerStore_.markers()) events.push_back({marker.time, marker.label});
    if (!exportController_->exportAsync(currentFile_.session.path, t0, t1, signalDurationSeconds_, outputPath, events)) {
        statusBar()->showMessage(i18n_->text("error.export.invalid_range", {{"t0", t0}, {"t1", t1}, {"duration", signalDurationSeconds_}}), 3000);
        return;
    }
    exportRunning_ = true;
    exportSegmentAction_->setEnabled(false);
    statusBar()->showMessage(i18n_->text("app.export.running"));
}

QString MainWindow::recordStatusSummary() const {
    return QString("active=%1 samples=%2 source_dropped=%3 writer_dropped=%4 events_dropped=%5")
        .arg(recordController_ != nullptr && recordController_->isActive())
        .arg(lastRecordSummary_.samplesWritten)
        .arg(lastRecordSummary_.sourceDroppedChunks)
        .arg(lastRecordSummary_.droppedChunks)
        .arg(lastRecordSummary_.droppedEvents);
}

QString MainWindow::exportStatusSummary() const {
    return QString("running=%1 path=%2 samples=%3 markers=%4")
        .arg(exportRunning_)
        .arg(lastExportResult_.path)
        .arg(lastExportResult_.nSamples)
        .arg(lastExportResult_.markerCount);
}

void MainWindow::buildMarkerDock() {
    markerDock_ = new QDockWidget(this);
    markerDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* content = new QWidget(markerDock_);
    auto* layout = new QVBoxLayout(content);
    markerTable_ = new QTableWidget(content);
    markerTable_->setColumnCount(3);
    markerTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    markerTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(markerTable_);
    auto* buttons = new QHBoxLayout();
    deleteMarkerButton_ = new QPushButton(content);
    clearMarkersButton_ = new QPushButton(content);
    exportMarkersButton_ = new QPushButton(content);
    buttons->addWidget(deleteMarkerButton_);
    buttons->addWidget(clearMarkersButton_);
    buttons->addWidget(exportMarkersButton_);
    layout->addLayout(buttons);
    markerDock_->setWidget(content);
    addDockWidget(Qt::RightDockWidgetArea, markerDock_);
    markerDock_->hide();
    connect(deleteMarkerButton_, &QPushButton::clicked, this, &MainWindow::deleteSelectedMarker);
    connect(clearMarkersButton_, &QPushButton::clicked, this, &MainWindow::clearMarkers);
    connect(exportMarkersButton_, &QPushButton::clicked, this, &MainWindow::exportMarkers);
    connect(markerTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!refreshingMarkerControls_ && item->column() == 1) {
            markerStore_.updateLabel(item->row(), item->text());
            waveformWidget_->setMarkers(markerStore_.markers());
        }
    });
}

void MainWindow::buildVideoDock() {
    videoDock_ = new QDockWidget(this);
    videoDock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    videoPanel_ = new VideoPanel(i18n_, videoDock_);
    videoDock_->setWidget(videoPanel_);
    addDockWidget(Qt::BottomDockWidgetArea, videoDock_);
    videoDock_->hide();
    connect(videoPanel_, &VideoPanel::offsetApplyRequested, this, [this]() {
        videoPanel_->applyOffset(playback_.position(), playback_.isPlaying());
        updatePlaybackDuration();
        statusBar()->showMessage(i18n_->text(
            "app.video.offset_applied",
            {{"offset", QString::number(videoPanel_->offsetSeconds(), 'f', 3)}}),
            3000);
    });
    connect(videoPanel_, &VideoPanel::durationKnown, this, [this](double) {
        updatePlaybackDuration();
        syncVideoToPlayback();
    });
    connect(videoPanel_, &VideoPanel::loadFailed, this, [this](const QString& detail) {
        statusBar()->showMessage(i18n_->text("app.video.load_failed", {{"detail", detail}}), 5000);
    });
}


void MainWindow::buildProcessingControls() {
    processingPanel_ = new EegProcessingPanel(this);
    processingPanel_->buildInto(browserToolbar_, processingState_);
    connect(processingPanel_, &EegProcessingPanel::changed, this, &MainWindow::onProcessingUiChanged);
}

void MainWindow::buildAnalysisDock() {
    analysisDock_ = new QDockWidget(this);
    analysisDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    analysisTabs_ = new QTabWidget(analysisDock_);
    analysisDock_->setWidget(analysisTabs_);
    addDockWidget(Qt::RightDockWidgetArea, analysisDock_);
    analysisDock_->hide();

    auto* psdPage = new QWidget(analysisTabs_);
    auto* psdLayout = new QVBoxLayout(psdPage);
    auto* psdControls = new QHBoxLayout();
    psdChannelCombo_ = new QComboBox(psdPage);
    psdModeCombo_ = new QComboBox(psdPage);
    psdDbCheck_ = new QCheckBox(psdPage);
    psdFmaxSpin_ = new QSpinBox(psdPage);
    psdFmaxSpin_->setRange(5, 500);
    psdFmaxSpin_->setValue(100);
    psdFmaxSpin_->setSuffix(" Hz");
    psdControls->addWidget(psdChannelCombo_);
    psdControls->addWidget(psdModeCombo_);
    psdControls->addWidget(psdDbCheck_);
    psdModeCombo_->addItem(QString());
    psdModeCombo_->addItem(QString());
    psdControls->addWidget(psdFmaxSpin_);
    psdControls->addStretch();
    spectrumWidget_ = new SpectrumWidget(psdPage);
    psdLayout->addLayout(psdControls);
    psdLayout->addWidget(spectrumWidget_);
    psdBandSummaryLabel_ = new QLabel(psdPage);
    psdBandSummaryLabel_->setWordWrap(true);
    psdLayout->addWidget(psdBandSummaryLabel_);
    analysisTabs_->addTab(psdPage, QString());

    auto* stftPage = new QWidget(analysisTabs_);
    auto* stftLayout = new QVBoxLayout(stftPage);
    auto* stftControls = new QHBoxLayout();
    stftChannelCombo_ = new QComboBox(stftPage);
    stftResolutionCombo_ = new QComboBox(stftPage);
    stftFmaxSpin_ = new QSpinBox(stftPage);
    stftFmaxSpin_->setRange(5, 500);
    stftFmaxSpin_->setValue(100);
    stftFmaxSpin_->setSuffix(" Hz");
    stftControls->addWidget(stftChannelCombo_);
    stftControls->addWidget(stftResolutionCombo_);
    stftControls->addWidget(stftFmaxSpin_);
    stftControls->addStretch();
    spectrogramWidget_ = new SpectrogramWidget(stftPage);
    stftLayout->addLayout(stftControls);
    stftLayout->addWidget(spectrogramWidget_);
    analysisTabs_->addTab(stftPage, QString());

    auto* trendPage = new QWidget(analysisTabs_);
    stftResolutionCombo_->addItem(QString());
    stftResolutionCombo_->addItem(QString());
    stftResolutionCombo_->addItem(QString());
    auto* trendLayout = new QVBoxLayout(trendPage);
    auto* trendControls = new QHBoxLayout();
    trendChannelCombo_ = new QComboBox(trendPage);
    trendEpochSpin_ = new QDoubleSpinBox(trendPage);
    trendEpochSpin_->setRange(0.5, 30.0);
    trendEpochSpin_->setValue(1.0);
    trendEpochSpin_->setSuffix(" s");
    trendControls->addWidget(trendChannelCombo_);
    trendControls->addWidget(trendEpochSpin_);
    trendControls->addStretch();
    trendWidget_ = new BandTrendWidget(trendPage);
    trendLayout->addLayout(trendControls);
    trendLayout->addWidget(trendWidget_);
    analysisTabs_->addTab(trendPage, QString());

    auto* coherencePage = new QWidget(analysisTabs_);
    auto* coherenceLayout = new QVBoxLayout(coherencePage);
    auto* coherenceControls = new QHBoxLayout();
    coherenceACombo_ = new QComboBox(coherencePage);
    coherenceBCombo_ = new QComboBox(coherencePage);
    coherenceFmaxSpin_ = new QSpinBox(coherencePage);
    coherenceFmaxSpin_->setRange(5, 500);
    coherenceFmaxSpin_->setValue(100);
    coherenceFmaxSpin_->setSuffix(" Hz");
    coherenceControls->addWidget(coherenceACombo_);
    coherenceControls->addWidget(coherenceBCombo_);
    coherenceControls->addWidget(coherenceFmaxSpin_);
    coherenceControls->addStretch();
    coherenceWidget_ = new SpectrumWidget(coherencePage);
    coherenceSummaryLabel_ = new QLabel(coherencePage);
    coherenceSummaryLabel_->setWordWrap(true);
    coherenceLayout->addLayout(coherenceControls);
    coherenceLayout->addWidget(coherenceWidget_);
    coherenceLayout->addWidget(coherenceSummaryLabel_);
    analysisTabs_->addTab(coherencePage, QString());

    analysisService_ = new AnalysisService(this);
    connect(analysisService_, &AnalysisService::completed, this, &MainWindow::handleAnalysisCompleted);
    connect(analysisService_, &AnalysisService::failed, this, &MainWindow::handleAnalysisFailed);
    connect(analysisTabs_, &QTabWidget::currentChanged, this, [this](int) { requestCurrentAnalysis(); });
    const auto reanalyze = [this]() { if (!refreshingAnalysisControls_) requestCurrentAnalysis(); };
    for (QComboBox* combo : {psdChannelCombo_, psdModeCombo_, stftChannelCombo_, stftResolutionCombo_, trendChannelCombo_, coherenceACombo_, coherenceBCombo_}) {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, reanalyze);
    }
    for (QSpinBox* spin : {psdFmaxSpin_, stftFmaxSpin_, coherenceFmaxSpin_}) {
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, reanalyze);
    }
    connect(psdDbCheck_, &QCheckBox::toggled, this, reanalyze);
    connect(trendEpochSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, reanalyze);
}

void MainWindow::refreshAnalysisControls() {
    if (analysisTabs_ == nullptr) {
        return;
    }
    refreshingAnalysisControls_ = true;
    const QStringList names = hasCurrentFile_ ? currentFile_.waveform.channelNames : QStringList{};
    for (QComboBox* combo : {psdChannelCombo_, stftChannelCombo_, trendChannelCombo_, coherenceACombo_, coherenceBCombo_}) {
        const int selected = combo->currentIndex();
        combo->clear();
        combo->addItems(names);
        combo->setCurrentIndex(std::clamp(selected, 0, std::max(0, combo->count() - 1)));
    }
    if (coherenceBCombo_->count() > 1 && coherenceBCombo_->currentIndex() == coherenceACombo_->currentIndex()) {
        coherenceBCombo_->setCurrentIndex(1);
    }
    const int nyquist = hasCurrentFile_ ? std::max(5, static_cast<int>(currentFile_.waveform.fs / 2.0)) : 500;
    for (QSpinBox* spin : {psdFmaxSpin_, stftFmaxSpin_, coherenceFmaxSpin_}) {
        spin->setMaximum(nyquist);
        spin->setValue(std::min(spin->value(), nyquist));
    }
    refreshingAnalysisControls_ = false;
}

void MainWindow::requestCurrentAnalysis() {
    if (!hasCurrentFile_ || refreshingAnalysisControls_ || analysisService_ == nullptr) {
        return;
    }
    AnalysisRequest request;
    request.data = matrixFromFloatChannels(currentFile_.waveform.data);
    request.fs = currentFile_.waveform.fs;
    request.tStart = currentFile_.waveform.t0;
    const int currentTab = analysisTabs_->currentIndex();
    if (currentTab == 0) {
        request.kind = psdModeCombo_->currentIndex() == 1 ? AnalysisKind::Fft : AnalysisKind::Psd;
        request.channel = psdChannelCombo_->currentIndex();
        request.fmax = psdFmaxSpin_->value();
    } else if (currentTab == 1) {
        request.kind = AnalysisKind::Stft;
        request.channel = stftChannelCombo_->currentIndex();
        request.fmax = stftFmaxSpin_->value();
        const int base = std::max(16, static_cast<int>(request.fs / 2.0));
        const int factors[] = {1, 2, 4};
        request.nperseg = base * factors[std::clamp(stftResolutionCombo_->currentIndex(), 0, 2)];
    } else if (currentTab == 2) {
        request.kind = AnalysisKind::BandTrend;
        request.channel = trendChannelCombo_->currentIndex();
        request.epochSamples = std::max(2, static_cast<int>(trendEpochSpin_->value() * request.fs));
    } else {
        request.kind = AnalysisKind::Coherence;
        request.channel = coherenceACombo_->currentIndex();
        request.channelB = coherenceBCombo_->currentIndex();
        request.fmax = coherenceFmaxSpin_->value();
    }
    if (request.channel < 0 || (request.kind == AnalysisKind::Coherence && request.channelB < 0)) {
        return;
    }
    analysisGeneration_ = analysisService_->request(request);
}

void MainWindow::handleAnalysisCompleted(int generation, const AnalysisResult& result) {
    if (generation != analysisGeneration_) {
        return;
    }
    switch (result.kind) {
    case AnalysisKind::Psd:
    case AnalysisKind::Fft: {
        const bool fft = result.kind == AnalysisKind::Fft;
        const bool asDb = !fft && psdDbCheck_->isChecked();
        dsp::Spectrum spectrum = result.spectrum;
        if (asDb) {
            for (double& value : spectrum.values) {
                value = 10.0 * std::log10(std::max(value, 1e-12));
            }
        }
        const auto limit = std::upper_bound(spectrum.frequencies.begin(), spectrum.frequencies.end(), static_cast<double>(psdFmaxSpin_->value()));
        const std::size_t keep = static_cast<std::size_t>(limit - spectrum.frequencies.begin());
        spectrum.frequencies.resize(keep);
        spectrum.values.resize(keep);
        spectrumWidget_->setSpectrum(
            spectrum,
            fft ? i18n_->text("app.analysis.fft") : i18n_->text("app.analysis.psd"),
            fft ? i18n_->text("app.analysis.amplitude") : (asDb ? i18n_->text("app.analysis.db") : i18n_->text("app.analysis.psd_unit")));
        if (!fft && !asDb) {
            QStringList powers;
            for (const dsp::Band& band : dsp::eegBands()) {
                powers.push_back(QString("%1=%2").arg(band.key).arg(dsp::bandPower(result.spectrum, band.low, band.high), 0, 'f', 2));
            }
            psdBandSummaryLabel_->setText(powers.join(" | "));
        } else {
            psdBandSummaryLabel_->clear();
        }
        break;
    }
    case AnalysisKind::Stft:
        spectrogramWidget_->setSpectrogram(result.spectrogram);
        break;
    case AnalysisKind::BandTrend:
        trendWidget_->setTrend(result.trend.times, result.trend.powers);
        break;

    case AnalysisKind::Coherence: {
        coherenceWidget_->setSpectrum(result.spectrum, i18n_->text("app.analysis.coherence"), i18n_->text("app.analysis.coherence"), true);
        QStringList values;
        for (const dsp::Band& band : dsp::eegBands()) {
            double sum = 0.0;
            int count = 0;
            for (std::size_t i = 0; i < result.spectrum.frequencies.size(); ++i) {
                if (result.spectrum.frequencies[i] >= band.low && result.spectrum.frequencies[i] <= band.high) {
                    sum += result.spectrum.values[i];
                    ++count;
                }
            }
            values.push_back(QString("%1=%2").arg(band.key).arg(count > 0 ? sum / count : 0.0, 0, 'f', 2));
        }
        coherenceSummaryLabel_->setText(values.join(" | "));
        break;
    }
    }
}

void MainWindow::togglePlayback() {
    if (!hasCurrentFile_) return;
    if (playback_.isPlaying()) {
        playback_.pause();
        playbackTimer_.stop();
    } else {
        if (playback_.position() < currentFile_.waveform.t0 || playback_.position() >= currentFile_.waveform.t1) {
            requestWindow(playback_.position());
        }
        playback_.play();
        playbackElapsed_.restart();
        playbackTimer_.start();
    }
    syncVideoToPlayback();
    refreshPlaybackControls();
    refreshPersistentStatus();
}

void MainWindow::advancePlayback() {
    const bool wasPlaying = playback_.isPlaying();
    const double elapsedSeconds = playbackElapsed_.restart() / 1000.0;
    playback_.advance(elapsedSeconds);
    const double prefillAt = currentFile_.waveform.t1 - std::min(1.0, viewState_.windowSeconds * 0.25);
    const bool hasNextWindow = currentFile_.waveform.t1 + 1e-9 < signalDurationSeconds_;
    if (playback_.isPlaying() && hasNextWindow && !playbackPrefetchPending_ && playback_.position() >= prefillAt) {
        playbackPrefetchPending_ = true;
        requestWindow(playback_.position());
    }
    if (wasPlaying && !playback_.isPlaying() && playback_.position() == 0.0) {
        playbackPrefetchPending_ = true;
        requestWindow(0.0);
    }
    if (!playback_.isPlaying()) {
        playbackTimer_.stop();
    }
    syncVideoToPlayback();
    refreshPlaybackControls();
    refreshPersistentStatus();
}

void MainWindow::refreshPlaybackControls() {
    if (playAction_ == nullptr) {
        return;
    }
    playAction_->setIcon(style()->standardIcon(playback_.isPlaying() ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    playAction_->setText(i18n_->text(playback_.isPlaying() ? "app.playback.pause" : "app.playback.play"));
    playAction_->setToolTip(playAction_->text());
    waveformWidget_->setCursorTime(playback_.position());
    if (positionSpin_ != nullptr) {
        const QSignalBlocker blocker(positionSpin_);
        positionSpin_->setValue(std::min(playback_.position(), positionSpin_->maximum()));
    }
    if (playbackProgress_ != nullptr && !playbackProgress_->isSliderDown()) {
        const QSignalBlocker blocker(playbackProgress_);
        const int value = playback_.duration() > 0.0
            ? static_cast<int>(10000.0 * playback_.position() / playback_.duration())
            : 0;
        playbackProgress_->setValue(value);
    }
}

void MainWindow::syncVideoToPlayback() {
    if (videoPanel_ != nullptr && videoPanel_->isLoaded()) {
        videoPanel_->syncTo(playback_.position(), playback_.isPlaying());
    }
}

void MainWindow::updatePlaybackDuration() {
    const double videoEnd = videoPanel_ != nullptr && videoPanel_->videoDurationSeconds() > 0.0
        ? videoPanel_->offsetSeconds() + videoPanel_->videoDurationSeconds()
        : 0.0;
    playback_.setDuration(std::max(signalDurationSeconds_, videoEnd));
    refreshBrowserControls();
    refreshPlaybackControls();
}

void MainWindow::addMarkerAt(double time) {
    if (!hasCurrentFile_) return;
    bool accepted = false;
    const QString label = QInputDialog::getText(this, i18n_->text("app.marker.add_title"), i18n_->text("app.marker.add_prompt", {{"time", QString::number(time, 'f', 3)}}), QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    markerStore_.add(time, label);
    if (recordController_ != nullptr && recordController_->isActive()) {
        recordController_->appendEvent(std::max(0.0, time - recordSourceStart_), label);
    }
    refreshMarkerControls();
}

void MainWindow::refreshMarkerControls() {
    if (markerTable_ == nullptr) return;
    refreshingMarkerControls_ = true;
    markerTable_->setRowCount(markerStore_.markers().size());
    for (int row = 0; row < markerStore_.markers().size(); ++row) {
        const Marker& marker = markerStore_.markers()[row];
        auto* time = new QTableWidgetItem(QString::number(marker.time, 'f', 3));
        time->setFlags(time->flags() & ~Qt::ItemIsEditable);
        markerTable_->setItem(row, 0, time);
        markerTable_->setItem(row, 1, new QTableWidgetItem(marker.label));
        auto* color = new QTableWidgetItem();
        color->setBackground(marker.color);
        color->setFlags(color->flags() & ~Qt::ItemIsEditable);
        markerTable_->setItem(row, 2, color);
    }
    refreshingMarkerControls_ = false;
    waveformWidget_->setMarkers(markerStore_.markers());
}

void MainWindow::deleteSelectedMarker() {
    if (markerTable_ != nullptr && markerStore_.remove(markerTable_->currentRow())) refreshMarkerControls();
}

void MainWindow::clearMarkers() {
    markerStore_.clear();
    refreshMarkerControls();
}

void MainWindow::exportMarkers() {
    if (!hasCurrentFile_ || !exportMarkersHandler_) return;
    QVector<SidecarEvent> events;
    events.reserve(markerStore_.markers().size());
    for (const Marker& marker : markerStore_.markers()) events.push_back({marker.time, marker.label});
    const SidecarResponse response = exportMarkersHandler_(currentFile_.session.path, events);
    if (!response.ok) statusBar()->showMessage(i18n_->text(QString("error.%1").arg(response.error.code), response.error.params));
}

void MainWindow::handleAnalysisFailed(int generation, const QString& code, const QString&) {
    if (generation != analysisGeneration_) {
        return;
    }
    if (code == "analysis.same_channel") {
        coherenceSummaryLabel_->setText(i18n_->text("error.analysis.same_channel"));
    } else {
        statusBar()->showMessage(i18n_->text(QString("error.%1").arg(code)));
    }
}

void MainWindow::chooseFile() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        i18n_->text("app.dialog.open_title"),
        QString(),
        i18n_->text("app.dialog.open_filter"));
    if (!path.isEmpty()) {
        openPathForBrowser(path);
    }
}

void MainWindow::chooseVideo() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        i18n_->text("app.video.open"),
        QString(),
        i18n_->text("app.video.filter"));
    if (!path.isEmpty()) {
        loadVideoForBrowser(path);
    }
}

bool MainWindow::loadVideoForBrowser(const QString& path) {
    if (videoPanel_ == nullptr || !videoPanel_->load(path)) {
        return false;
    }
    videoPanel_->setRate(playback_.speed());
    syncVideoToPlayback();
    videoDock_->show();
    statusBar()->showMessage(i18n_->text("app.video.loaded", {{"name", QFileInfo(path).fileName()}}), 3000);
    return true;
}

QString MainWindow::videoStatusSummary() const {
    return videoPanel_ != nullptr ? videoPanel_->statusSummary() : QString("unavailable");
}

void MainWindow::openPathForBrowser(const QString& path) {
    if (path.isEmpty() || !openFileHandler_) {
        return;
    }
    showFileOpenLoading(path);
    openFileHandler_(path);
}

void MainWindow::requestWindow(double t0) {
    if (!hasCurrentFile_ || !windowRequestHandler_) {
        return;
    }
    double requestedT0 = std::max(0.0, t0);
    if (currentFile_.session.duration > 0.0) {
        const double maxT0 = std::max(0.0, currentFile_.session.duration - viewState_.windowSeconds);
        requestedT0 = std::min(requestedT0, maxT0);
    }
    statusKey_ = "app.status.file_opening";
    statusParams_ = {{"path", currentFile_.session.path}};
    windowRequestHandler_(currentFile_.session.path, requestedT0, viewState_.windowSeconds);
}

void MainWindow::setLoading(bool loading) {
    if (openAction_ != nullptr) {
        openAction_->setEnabled(!loading);
    }
    if (openButton_ != nullptr) {
        openButton_->setEnabled(!loading);
    }
}

void MainWindow::refreshBrowserControls() {
    refreshingBrowserControls_ = true;
    if (timeWindowSpin_ != nullptr) {
        QSignalBlocker blocker(timeWindowSpin_);
        timeWindowSpin_->setValue(viewState_.windowSeconds);
    }
    if (amplitudeSpin_ != nullptr) {
        QSignalBlocker blocker(amplitudeSpin_);
        amplitudeSpin_->setValue(viewState_.amplitudeUv);
    }
    if (firstChannelSpin_ != nullptr) {
        QSignalBlocker blocker(firstChannelSpin_);
        const int maxFirst = std::max(0, channelCount() - 1);
        firstChannelSpin_->setRange(0, maxFirst);
        firstChannelSpin_->setValue(std::min(viewState_.firstVisibleChannel, maxFirst));
    }
    if (positionSpin_ != nullptr) {
        QSignalBlocker blocker(positionSpin_);
        positionSpin_->setRange(0.0, playback_.duration());
        positionSpin_->setValue(playback_.position());
    }
    refreshingBrowserControls_ = false;
    rebuildChannelControls();
}

int MainWindow::channelCount() const {
    if (!hasCurrentFile_) {
        return 0;
    }
    return static_cast<int>(currentFile_.waveform.data.size());
}

void MainWindow::rebuildChannelControls() {
    if (channelListLayout_ == nullptr) {
        return;
    }

    while (QLayoutItem* item = channelListLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    channelChecks_.clear();

    if (!hasCurrentFile_) {
        return;
    }

    const int nChannels = channelCount();
    for (int channel = 0; channel < nChannels; ++channel) {
        const QString label = channel < currentFile_.waveform.channelNames.size()
            ? currentFile_.waveform.channelNames[channel]
            : QString("Ch%1").arg(channel + 1);
        auto* check = new QCheckBox(label, channelListWidget_);
        check->setChecked(viewState_.isChannelVisible(channel));
        connect(check, &QCheckBox::toggled, this, [this, channel](bool checked) {
            if (refreshingBrowserControls_) {
                return;
            }
            viewState_.setChannelVisible(channel, checked);
            applyBrowserStateToWaveform();
        });
        channelListLayout_->addWidget(check);
        channelChecks_.push_back(check);
    }
    channelListLayout_->addStretch(1);
}

void MainWindow::retranslateUi() {
    setWindowTitle(i18n_->text("app.window_title", {{"product_name", version_.productName}, {"version", version_.version}}));
    centralLabel_->setText(i18n_->text(emptyStateKey_, emptyStateParams_));
    fileMenu_->setTitle(i18n_->text("app.menu.file"));
    openAction_->setText(i18n_->text("app.action.open"));
    openVideoAction_->setText(i18n_->text("app.video.open"));
    openButton_->setText(i18n_->text("app.action.open"));
    quitAction_->setText(i18n_->text("app.action.quit"));
    languageMenu_->setTitle(i18n_->text("app.menu.language"));
    viewMenu_->setTitle(i18n_->text("app.menu.view"));
    toggleChannelsAction_->setText(i18n_->text("app.view.channels"));
    toggleMarkersAction_->setText(i18n_->text("app.view.markers"));
    toggleAnalysisAction_->setText(i18n_->text("app.view.analysis"));
    toggleVideoAction_->setText(i18n_->text("app.view.video"));
    themeMenu_->setTitle(i18n_->text("app.menu.theme"));
    lightThemeAction_->setText(i18n_->text("app.theme.light"));
    darkThemeAction_->setText(i18n_->text("app.theme.dark"));
    for (QAction* action : languageActions_) {
        const QString locale = action->data().toString();
        action->setText(i18n_->text(QString("app.language.%1").arg(locale)));
        action->setChecked(locale == i18n_->locale());
    }

    previousWindowAction_->setToolTip(i18n_->text("app.browser.previous"));
    nextWindowAction_->setToolTip(i18n_->text("app.browser.next"));
    positionLabel_->setText(i18n_->text("app.browser.position"));
    timeWindowLabel_->setText(i18n_->text("app.browser.window"));
    amplitudeLabel_->setText(i18n_->text("app.browser.amplitude"));
    firstChannelLabel_->setText(i18n_->text("app.browser.first_channel"));
    channelDock_->setWindowTitle(i18n_->text("app.browser.channels"));
    if (processingPanel_ != nullptr) {
        processingPanel_->retranslateUi(i18n_);
    }
    analysisDock_->setWindowTitle(i18n_->text("app.analysis.panel"));
    analysisTabs_->setTabText(0, i18n_->text("app.analysis.psd"));
    analysisTabs_->setTabText(1, i18n_->text("app.analysis.stft"));
    analysisTabs_->setTabText(2, i18n_->text("app.analysis.trend"));
    spectrumWidget_->setLabels(i18n_->text("app.analysis.frequency"), i18n_->text("app.analysis.empty"));
    spectrogramWidget_->setLabels(
        i18n_->text("app.analysis.stft"),
        i18n_->text("app.analysis.time"),
        i18n_->text("app.analysis.frequency"),
        i18n_->text("app.analysis.empty"));
    trendWidget_->setLabels(
        i18n_->text("app.analysis.trend"),
        i18n_->text("app.analysis.time"),
        i18n_->text("app.analysis.empty"));
    coherenceWidget_->setLabels(i18n_->text("app.analysis.frequency"), i18n_->text("app.analysis.empty"));
    analysisTabs_->setTabText(3, i18n_->text("app.analysis.coherence"));
    psdModeCombo_->setItemText(0, i18n_->text("app.analysis.welch"));
    psdModeCombo_->setItemText(1, i18n_->text("app.analysis.fft"));
    psdDbCheck_->setText(i18n_->text("app.analysis.db_display"));
    stftResolutionCombo_->setItemText(0, i18n_->text("app.analysis.high_time"));
    stftResolutionCombo_->setItemText(1, i18n_->text("app.analysis.balanced"));
    stftResolutionCombo_->setItemText(2, i18n_->text("app.analysis.high_frequency"));
    markerModeAction_->setText(i18n_->text("app.marker.mode"));
    markerModeAction_->setToolTip(i18n_->text("app.marker.mode_tooltip"));
    playAction_->setToolTip(i18n_->text(playback_.isPlaying() ? "app.playback.pause" : "app.playback.play"));
    speedLabel_->setText(i18n_->text("app.playback.speed"));
    markerDock_->setWindowTitle(i18n_->text("app.marker.panel"));
    markerTable_->setHorizontalHeaderLabels({i18n_->text("app.marker.time"), i18n_->text("app.marker.label"), i18n_->text("app.marker.color")});
    deleteMarkerButton_->setText(i18n_->text("app.marker.delete"));
    clearMarkersButton_->setText(i18n_->text("app.marker.clear"));
    exportMarkersButton_->setText(i18n_->text("app.marker.export"));
    recordAction_->setText(i18n_->text(recordController_ != nullptr && recordController_->isActive() ? "app.record.stop" : "app.record.start"));
    exportSegmentAction_->setText(i18n_->text("app.export.action"));
    videoDock_->setWindowTitle(i18n_->text("app.video.panel"));
    videoPanel_->retranslateUi();

    if (statusKey_ == "app.status.sidecar_failed" || statusKey_ == "app.status.file_failed") {
        statusBar()->showMessage(i18n_->text(
            statusKey_,
            {{"error", i18n_->text(QString("error.%1").arg(lastError_.code), lastError_.params)}}));
        refreshPersistentStatus();
        return;
    }
    statusBar()->showMessage(i18n_->text(statusKey_, statusParams_));
    refreshPersistentStatus();
}

void MainWindow::setSidecarConnecting() {
    statusKey_ = "app.status.sidecar_connecting";
    statusParams_ = {};
    retranslateUi();
}

void MainWindow::setSidecarConnected() {
    statusKey_ = "app.status.sidecar_connected";
    statusParams_ = {};
    retranslateUi();
}

void MainWindow::setSidecarFailed(const AppError& error) {
    statusKey_ = "app.status.sidecar_failed";
    lastError_ = error;
    statusParams_ = {};
    retranslateUi();
}

void MainWindow::showFileOpenLoading(const QString& path) {
    setLoading(true);
    statusKey_ = "app.status.file_opening";
    statusParams_ = {{"path", path}};
    retranslateUi();
}

void MainWindow::showFileOpenResult(const FileOpenResult& result) {
    setLoading(false);
    const bool sameSession = hasCurrentFile_ && currentFile_.session.path == result.session.path;
    currentFile_ = result;
    rawWaveform_ = result.waveform;
    hasCurrentFile_ = true;
    if (!sameSession && recordController_ != nullptr && recordController_->isActive()) {
        recordController_->stop();
    }
    playbackPrefetchPending_ = false;
    centralStack_->setCurrentWidget(waveformWidget_);
    browserToolbar_->show();
    if (!sameSession) {
        playback_.pause();
        playbackTimer_.stop();
        signalDurationSeconds_ = result.session.duration;
        updatePlaybackDuration();
        playback_.seek(result.waveform.t0);
        QVector<QPair<double, QString>> events;
        events.reserve(result.session.events.size());
        for (const SidecarEvent& event : result.session.events) {
            events.push_back({event.time, event.label});
        }
        markerStore_.replaceFromEvents(events);
    }
    channelDock_->show();
    refreshBrowserControls();
    refreshProcessingControls();
    applyProcessingAndDisplay();
    refreshAnalysisControls();
    analysisDock_->show();
    refreshPersistentStatus();
    markerDock_->show();
    refreshPlaybackControls();
    refreshMarkerControls();
    syncVideoToPlayback();
    requestCurrentAnalysis();
}

void MainWindow::showFileOpenError(const AppError& error) {
    setLoading(false);
    playbackPrefetchPending_ = false;
    statusKey_ = "app.status.file_failed";
    lastError_ = error;
    statusParams_ = {};
    if (!hasCurrentFile_) {
        emptyStateKey_ = QString("error.%1").arg(error.code);
        emptyStateParams_ = error.params;
        centralStack_->setCurrentWidget(emptyPage_);
    }
    retranslateUi();
}

void MainWindow::setBrowserViewState(const WaveformViewState& state) {
    viewState_ = state;
    refreshBrowserControls();
    applyBrowserStateToWaveform();
}

void MainWindow::setFirstVisibleChannelForBrowser(int channel) {
    viewState_.setFirstVisibleChannel(channel);
    refreshBrowserControls();
    applyBrowserStateToWaveform();
}

void MainWindow::setChannelVisibleForBrowser(int channel, bool visible) {
    viewState_.setChannelVisible(channel, visible);
    refreshBrowserControls();
    applyBrowserStateToWaveform();
}

void MainWindow::setOpenFileHandler(std::function<void(const QString&)> handler) {
    openFileHandler_ = std::move(handler);
}

void MainWindow::setWindowRequestHandler(std::function<void(const QString&, double, double)> handler) {
    windowRequestHandler_ = std::move(handler);
}

void MainWindow::setExportMarkersHandler(std::function<SidecarResponse(const QString&, const QVector<SidecarEvent>&)> handler) {
    exportMarkersHandler_ = std::move(handler);
}

void MainWindow::setBrowserWindowSecondsForBrowser(double seconds) {
    if (timeWindowSpin_ != nullptr) {
        timeWindowSpin_->setValue(seconds);
        return;
    }
    viewState_.setWindowSeconds(seconds);
    applyBrowserStateToWaveform();
}

void MainWindow::applyBrowserStateToWaveform() {
    waveformWidget_->setViewState(viewState_);
    if (!hasCurrentFile_) {
        return;
    }

    const WaveformDisplayModel model = waveformDisplayModel(currentFile_.waveform, viewState_, 32);
    statusKey_ = "app.status.file_window";
    statusParams_ = {
        {"format", currentFile_.session.format},
        {"t0", currentFile_.waveform.t0},
        {"t1", currentFile_.waveform.t1},
        {"amplitude", viewState_.amplitudeUv},
        {"n_visible", static_cast<int>(model.data.size())},
    };
    retranslateUi();
}

void MainWindow::refreshProcessingControls() {
    if (processingPanel_ == nullptr) {
        return;
    }
    processingPanel_->refreshFromState(processingState_);
}

void MainWindow::onProcessingUiChanged() {
    if (processingPanel_ == nullptr) {
        return;
    }
    processingPanel_->writeToState(processingState_);
    refreshProcessingControls();
    applyProcessingAndDisplay();
}

void MainWindow::applyProcessingAndDisplay() {
    if (!hasCurrentFile_) {
        return;
    }
    currentFile_.waveform = applyProcessingToSlice(rawWaveform_, processingState_);
    waveformWidget_->setSlice(currentFile_.waveform);
    refreshAnalysisControls();
    requestCurrentAnalysis();
    rebuildChannelControls();
    applyBrowserStateToWaveform();
}

void MainWindow::setProcessingViewStateForBrowser(const ProcessingViewState& state) {
    processingState_ = state;
    refreshProcessingControls();
    applyProcessingAndDisplay();
}

QString MainWindow::processingSummary() const {
    return processingState_.summary();
}

QString MainWindow::browserStatusText() const {
    return statusBar()->currentMessage();
}

QString MainWindow::channelCheckSummary() const {
    QStringList parts;
    for (int channel = 0; channel < channelChecks_.size(); ++channel) {
        const QCheckBox* check = channelChecks_[channel];
        parts.push_back(QString("%1:%2").arg(check->text(), check->isChecked() ? "on" : "off"));
    }
    return parts.join(",");
}

QString MainWindow::visibleDisplayChannelSummary() const {
    if (!hasCurrentFile_) {
        return {};
    }

    QStringList parts;
    const int nChannels = channelCount();
    const int first = std::min(std::max(0, viewState_.firstVisibleChannel), nChannels);
    for (int channel = first; channel < nChannels; ++channel) {
        if (viewState_.isChannelVisible(channel)) {
            parts.push_back(QString::number(channel));
        }
    }
    return parts.join(",");
}

QString MainWindow::browserControlSummary() const {
    return QString("first=%1 visible=%2 checks=%3")
        .arg(viewState_.firstVisibleChannel)
        .arg(visibleDisplayChannelSummary())
        .arg(channelCheckSummary());
}

}  // namespace bf
