#include "ui/main_window.h"
#include "ui/record_metadata_dialog.h"
#include "ui/collapsible_panel.h"

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
#include <QScopedValueRollback>
#include <QHeaderView>
#include <QInputDialog>
#include <QIcon>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QSpinBox>
#include <QMenuBar>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTime>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace bf {

MainWindow::MainWindow(const VersionInfo& version, I18nService* i18n, const QString& repoRoot, QWidget* parent)
    : QMainWindow(parent), version_(version), repoRoot_(repoRoot), i18n_(i18n) {
    setWindowIcon(QIcon(QStringLiteral(":/branding/app_icon.png")));
    centralStack_ = new QStackedWidget(this);
    centralStack_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    centralStack_->setMinimumSize(320, 180);
    emptyPage_ = new QWidget(centralStack_);
    emptyPage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* emptyLayout = new QVBoxLayout(emptyPage_);
    emptyLayout->setAlignment(Qt::AlignCenter);
    auto* logoLabel = new QLabel(emptyPage_);
    logoLabel->setObjectName(QStringLiteral("brandLogo"));
    logoLabel->setPixmap(QPixmap(QStringLiteral(":/branding/logo_with_text.png")).scaledToWidth(300, Qt::SmoothTransformation));
    logoLabel->setAlignment(Qt::AlignCenter);
    centralLabel_ = new QLabel(emptyPage_);
    centralLabel_->setAlignment(Qt::AlignCenter);
    openButton_ = new QPushButton(emptyPage_);
    openButton_->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    openButton_->setMinimumWidth(140);
    emptyLayout->addWidget(logoLabel, 0, Qt::AlignCenter);
    emptyLayout->addSpacing(12);
    emptyLayout->addWidget(centralLabel_, 0, Qt::AlignCenter);
    emptyLayout->addWidget(openButton_, 0, Qt::AlignCenter);
    waveformWidget_ = new WaveformWidget(this);
    centralStack_->addWidget(emptyPage_);
    centralStack_->addWidget(waveformWidget_);
    waveformWidget_->setMinimumSize(320, 180);
    // Keep the menu bar inside the Qt widget hierarchy on Windows so palette,
    // stylesheet and action geometry are deterministic.
    menuBar()->setNativeMenuBar(false);
    menuBar()->setAttribute(Qt::WA_StyledBackground, true);

    buildBrowserControls();
    buildProcessingControls();
    buildAnalysisDock();
    buildPlaybackControls();
    buildMarkerDock();
    buildVideoDock();
    buildRecordControls();
    for (auto* combo : {psdChannelCombo_, stftChannelCombo_, trendChannelCombo_, coherenceACombo_, coherenceBCombo_}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(6);
        combo->setMinimumWidth(70);
    }
    buildFixedWorkspace();
    // Build all top-level menus consecutively after toolbar/dock layout is
    // complete. This keeps their mouse regions in one stable QMenuBar layout.
    buildFileMenu();
    buildLanguageMenu();
    buildViewMenu();
    buildThemeMenu();
    // 订阅 I18nService 语言切换信号，统一刷新常驻 UI；避免在各回调里手工 retranslate。
    connect(i18n_, &I18nService::languageChanged, this, [this]() {
        retranslateUi();
    });
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
    // Apply theme only after menu titles/actions are translated, otherwise the
    // first QMenuBar items can stay blank under stylesheet on Windows.
    applyTheme(darkTheme_);
}

void MainWindow::buildFixedWorkspace() {
    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(6);
    layout->addWidget(browserToolbar_);
    auto* workspace = new QSplitter(Qt::Horizontal, root);
    workspace->setObjectName("eegWorkspaceSplitter");
    workspace->setChildrenCollapsible(false);
    signalSplitter_ = new QSplitter(Qt::Vertical, workspace);
    signalSplitter_->setObjectName("eegSignalSplitter");
    signalSplitter_->setChildrenCollapsible(false);
    auto* analysisSplit = new QSplitter(Qt::Vertical, workspace);
    analysisSplit->setObjectName("eegAnalysisSplitter");
    analysisSplit->setChildrenCollapsible(false);
    const auto fixedPanel = [](QDockWidget* panel, const char* name) {
        panel->setObjectName(name);
        panel->setFeatures(QDockWidget::NoDockWidgetFeatures);
        panel->setAllowedAreas(Qt::NoDockWidgetArea);
        panel->setTitleBarWidget(new QWidget(panel));
    };
    fixedPanel(channelDock_, "eegChannelPanel");
    fixedPanel(markerDock_, "eegMarkerPanel");
    fixedPanel(analysisDock_, "eegAnalysisPanel");
    fixedPanel(videoDock_, "eegVideoPanel");
    signalSplitter_->addWidget(centralStack_);
    signalSplitter_->addWidget(videoDock_);
    signalSplitter_->setStretchFactor(0, 3);
    signalSplitter_->setStretchFactor(1, 1);
    analysisSplit->addWidget(analysisDock_);
    auto* parameterScroll = new QScrollArea(analysisSplit);
    parameterScroll->setObjectName("eegParameterScroll");
    parameterScroll->setWidgetResizable(true);
    parameterScroll->setMinimumHeight(48);
    parameterScroll->setFrameShape(QFrame::NoFrame);
    parameterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* parameters = new QWidget(parameterScroll);
    auto* parameterLayout = new QVBoxLayout(parameters);
    parameterLayout->setContentsMargins(0, 0, 2, 0);
    parameterLayout->setSpacing(4);
    channelScroll_->setMinimumHeight(80);
    channelScroll_->setMaximumHeight(150);
    parameterLayout->addWidget(channelDock_);
    filterPanel_ = new CollapsiblePanel(i18n_->text("app.processing.panel"), parameters);
    filterPanel_->setObjectName("eegFilterPanel");
    filterPanel_->addWidget(filterToolbar_);
    channelBandpassControls_ = new ChannelBandpassControls(i18n_, filterPanel_);
    channelBandpassControls_->changed = [this](int channel, std::optional<ChannelBandpassUi> setting) {
        if (setting) processingState_.setChannelBandpass(channel, *setting);
        else processingState_.clearChannelBandpass(channel);
        applyProcessingAndDisplay();
    };
    filterPanel_->addWidget(channelBandpassControls_);
    parameterLayout->addWidget(filterPanel_);
    markerTable_->setMinimumHeight(70);
    markerTable_->setMaximumHeight(170);
    parameterLayout->addWidget(markerDock_);
    parameterLayout->addStretch();
    parameterScroll->setWidget(parameters);
    analysisSplit->addWidget(parameterScroll);
    analysisSplit->setStretchFactor(0, 2);
    analysisSplit->setStretchFactor(1, 1);
    analysisSplit->setSizes({280, 180});
    workspace->addWidget(signalSplitter_);
    workspace->addWidget(analysisSplit);
    workspace->setStretchFactor(0, 2);
    workspace->setStretchFactor(1, 1);
    workspace->setSizes({560, 320});
    layout->addWidget(workspace, 1);
    playbackBar_ = new QWidget(root);
    playbackBar_->setObjectName("eegPlaybackBar");
    auto* playbackLayout = new QVBoxLayout(playbackBar_);
    playbackLayout->setContentsMargins(0, 0, 0, 0);
    playbackLayout->setSpacing(2);
    playbackLayout->addWidget(playbackToolbar_);
    playbackLayout->addWidget(progressRow_);
    layout->addWidget(playbackBar_);
    playbackBar_->hide();
    setCentralWidget(root);
}

void MainWindow::buildFileMenu() {
    fileMenu_ = menuBar()->addMenu(i18n_->text("app.menu.file"));
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
    languageMenu_ = menuBar()->addMenu(i18n_->text("app.menu.language"));
    // 语言选项互斥；stable 菜单仅展示 supportedLocales（zh_CN/en_US/ja_JP/zh_TW），
    // pseudo 与未审校 locale 不在此列表，不暴露给用户。
    languageGroup_ = new QActionGroup(this);
    languageGroup_->setExclusive(true);
    const QStringList locales = i18n_->supportedLocales();
    for (const QString& locale : locales) {
        auto* action = languageMenu_->addAction(QString());
        action->setCheckable(true);
        action->setActionGroup(languageGroup_);
        action->setData(locale);
        // 用户菜单切换走持久化 setLocale；失败时保留当前语言，不产生半切换状态。
        // languageChanged 信号会统一触发 retranslateUi，此处不再手工刷新。
        QObject::connect(action, &QAction::triggered, this, [this, locale]() {
            if (locale == i18n_->locale()) {
                return;
            }
            QString error;
            if (!i18n_->setLocale(locale, &error)) {
                // 切换失败：恢复原选中项，避免勾选状态与实际语言不一致。
                QSignalBlocker block(languageGroup_);
                for (QAction* a : languageActions_) {
                    a->setChecked(a->data().toString() == i18n_->locale());
                }
                qWarning("语言切换失败 locale=%s err=%s",
                         qPrintable(locale), qPrintable(error));
            }
        });
        languageActions_.append(action);
    }
}

void MainWindow::buildViewMenu() {
    viewMenu_ = menuBar()->addMenu(i18n_->text("app.menu.view"));
    toggleChannelsAction_ = viewMenu_->addAction(QString());
    toggleChannelsAction_->setObjectName("eegToggleChannels");
    toggleMarkersAction_ = viewMenu_->addAction(QString());
    toggleMarkersAction_->setObjectName("eegToggleMarkers");
    toggleAnalysisAction_ = viewMenu_->addAction(QString());
    toggleAnalysisAction_->setObjectName("eegToggleAnalysis");
    toggleVideoAction_ = viewMenu_->addAction(QString());
    toggleVideoAction_->setObjectName("eegToggleVideo");
    auto wireToggle = [this](QAction* action, QDockWidget* dock) {
        action->setCheckable(true);
        action->setChecked(!dock->isHidden());
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
    themeMenu_ = menuBar()->addMenu(i18n_->text("app.menu.theme"));
    themeGroup_ = new QActionGroup(this);
    themeGroup_->setExclusive(true);
    lightThemeAction_ = themeMenu_->addAction(QString());
    lightThemeAction_->setCheckable(true);
    darkThemeAction_ = themeMenu_->addAction(QString());
    darkThemeAction_->setCheckable(true);
    themeGroup_->addAction(lightThemeAction_);
    themeGroup_->addAction(darkThemeAction_);
    // Default to light lavender + white for readable contrast.
    lightThemeAction_->setChecked(true);
    connect(lightThemeAction_, &QAction::triggered, this, [this]() { applyTheme(false); });
    connect(darkThemeAction_, &QAction::triggered, this, [this]() { applyTheme(true); });
    // Do not applyTheme here: menu titles are still empty until retranslateUi().
}

void MainWindow::applyTheme(bool dark) {
    darkTheme_ = dark;
    // Brand accent: soft Tsinghua lavender. Light theme prioritizes readable
    // black text on white / pale-lavender surfaces.
    QPalette palette;
    if (dark) {
        const QColor accentDark("#A78BFA");
        palette.setColor(QPalette::Window, QColor("#1E1B2E"));
        palette.setColor(QPalette::Base, QColor("#151221"));
        palette.setColor(QPalette::AlternateBase, QColor("#2A2540"));
        palette.setColor(QPalette::WindowText, QColor("#F5F3FF"));
        palette.setColor(QPalette::Text, QColor("#F5F3FF"));
        palette.setColor(QPalette::Button, QColor("#2A2540"));
        palette.setColor(QPalette::ButtonText, QColor("#F5F3FF"));
        palette.setColor(QPalette::BrightText, QColor("#FFFFFF"));
        palette.setColor(QPalette::PlaceholderText, QColor("#C4B5FD"));
        palette.setColor(QPalette::Highlight, accentDark);
        palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
        palette.setColor(QPalette::Link, QColor("#C4B5FD"));
        palette.setColor(QPalette::ToolTipBase, QColor("#2A2540"));
        palette.setColor(QPalette::ToolTipText, QColor("#F5F3FF"));
        palette.setColor(QPalette::Light, QColor("#3B3358"));
        palette.setColor(QPalette::Mid, QColor("#2A2540"));
        palette.setColor(QPalette::Dark, QColor("#151221"));
        palette.setColor(QPalette::Shadow, QColor("#0B0A12"));
    } else {
        // Light lavender + white: high contrast for menu/button/status text.
        const QColor accentLight("#7C3AED");
        palette.setColor(QPalette::Window, QColor("#F7F3FF"));
        palette.setColor(QPalette::Base, QColor("#FFFFFF"));
        palette.setColor(QPalette::AlternateBase, QColor("#EFE7FF"));
        palette.setColor(QPalette::WindowText, QColor("#1F1235"));
        palette.setColor(QPalette::Text, QColor("#1F1235"));
        palette.setColor(QPalette::Button, QColor("#FFFFFF"));
        palette.setColor(QPalette::ButtonText, QColor("#1F1235"));
        palette.setColor(QPalette::BrightText, QColor("#FFFFFF"));
        palette.setColor(QPalette::PlaceholderText, QColor("#6B5B95"));
        palette.setColor(QPalette::Highlight, accentLight);
        palette.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
        palette.setColor(QPalette::Link, QColor("#6D28D9"));
        palette.setColor(QPalette::ToolTipBase, QColor("#FFFFFF"));
        palette.setColor(QPalette::ToolTipText, QColor("#1F1235"));
        palette.setColor(QPalette::Light, QColor("#FFFFFF"));
        palette.setColor(QPalette::Mid, QColor("#E9DFFF"));
        palette.setColor(QPalette::Dark, QColor("#C4B5FD"));
        palette.setColor(QPalette::Shadow, QColor("#A78BFA"));
    }
    // Disabled roles still need readable contrast.
    const QColor disabledText = dark ? QColor("#9CA3AF") : QColor("#8B7EAE");
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);

    setPalette(palette);
    for (QWidget* child : findChildren<QWidget*>()) {
        // Palette propagation is enough. Forcing autoFillBackground on every
        // descendant breaks QMenuBar action painting on Windows.
        child->setAutoFillBackground(false);
        child->setPalette(palette);
    }
    // Windows native menu bar often ignores QPalette; style sheet keeps labels readable.
    if (!dark) {
        setStyleSheet(
            "QMainWindow { background: #F7F3FF; color: #1F1235; }"
            "QMenuBar { background: #FFFFFF; color: #1F1235; border-bottom: 1px solid #E9DFFF; }"
            "QMenuBar::item { background: transparent; color: #1F1235; padding: 4px 10px; }"
            "QMenuBar::item:selected { background: #EFE7FF; color: #1F1235; }"
            "QMenu { background: #FFFFFF; color: #1F1235; border: 1px solid #E9DFFF; }"
            "QMenu::item:selected { background: #EFE7FF; color: #1F1235; }"
            "QToolBar { background: #FFFFFF; color: #1F1235; border-bottom: 1px solid #E9DFFF; spacing: 6px; }"
            "QStatusBar { background: #FFFFFF; color: #1F1235; border-top: 1px solid #E9DFFF; }"
            "QPushButton { background: #FFFFFF; color: #1F1235; border: 1px solid #C4B5FD; border-radius: 4px; padding: 4px 12px; }"
            "QPushButton:hover { background: #EFE7FF; }"
            "QPushButton:pressed { background: #E9DFFF; }"
            "QToolButton { color: #1F1235; }"
            "QLabel { color: #1F1235; }"
            "QDockWidget { color: #1F1235; titlebar-close-icon: none; }"
            "QDockWidget::title { background: #EFE7FF; color: #1F1235; padding: 4px; }"
            "QTabBar::tab { background: #FFFFFF; color: #1F1235; border: 1px solid #E9DFFF; padding: 4px 10px; }"
            "QTabBar::tab:selected { background: #EFE7FF; color: #1F1235; }"
            "QHeaderView::section { background: #EFE7FF; color: #1F1235; border: 1px solid #E9DFFF; }"
            "QTableWidget { background: #FFFFFF; color: #1F1235; gridline-color: #E9DFFF; }"
            "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #FFFFFF; color: #1F1235; border: 1px solid #C4B5FD; selection-background-color: #7C3AED; selection-color: #FFFFFF; }"
        );
    } else {
        setStyleSheet(
            "QMainWindow { background: #1E1B2E; color: #F5F3FF; }"
            "QMenuBar { background: #151221; color: #F5F3FF; border-bottom: 1px solid #2A2540; }"
            "QMenuBar::item { background: transparent; color: #F5F3FF; padding: 4px 10px; }"
            "QMenuBar::item:selected { background: #2A2540; color: #FFFFFF; }"
            "QMenu { background: #151221; color: #F5F3FF; border: 1px solid #2A2540; }"
            "QMenu::item:selected { background: #2A2540; color: #FFFFFF; }"
            "QToolBar { background: #151221; color: #F5F3FF; border-bottom: 1px solid #2A2540; }"
            "QStatusBar { background: #151221; color: #F5F3FF; border-top: 1px solid #2A2540; }"
            "QPushButton { background: #2A2540; color: #F5F3FF; border: 1px solid #3B3358; border-radius: 4px; padding: 4px 12px; }"
            "QPushButton:hover { background: #3B3358; }"
            "QToolButton { color: #F5F3FF; }"
            "QLabel { color: #F5F3FF; }"
            "QDockWidget { color: #F5F3FF; }"
            "QDockWidget::title { background: #2A2540; color: #F5F3FF; padding: 4px; }"
            "QTabBar::tab { background: #151221; color: #F5F3FF; border: 1px solid #2A2540; padding: 4px 10px; }"
            "QTabBar::tab:selected { background: #2A2540; color: #FFFFFF; }"
            "QHeaderView::section { background: #2A2540; color: #F5F3FF; border: 1px solid #3B3358; }"
            "QTableWidget { background: #151221; color: #F5F3FF; gridline-color: #2A2540; }"
            "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #151221; color: #F5F3FF; border: 1px solid #3B3358; selection-background-color: #A78BFA; selection-color: #FFFFFF; }"
        );
    }
    // Stylesheets can rematerialize some palette roles on Fusion/Windows; re-apply
    // the intended palette after setStyleSheet so Highlight/Text stay correct.
    setPalette(palette);
    for (QWidget* child : findChildren<QWidget*>()) {
        if (qobject_cast<QMenuBar*>(child) != nullptr || qobject_cast<QMenu*>(child) != nullptr) {
            child->setAutoFillBackground(false);
        }
        child->setPalette(palette);
    }
    // Menu actions are created with translated, non-empty titles after the
    // complete window layout. Theme application must not mutate or rebind them.
    if (menuBar() != nullptr) {
        menuBar()->setPalette(palette);
        menuBar()->updateGeometry();
        menuBar()->update();
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
    browserToolbar_ = new ControlBar(this);
    browserToolbar_->setObjectName("eegSignalToolbar");
    playbackToolbar_ = new ControlBar(this);
    filterToolbar_ = new ControlBar(this);
    filterToolbar_->setObjectName("eegFilterControls");
    progressRow_ = new QWidget(this);
    auto* progressLayout = new QHBoxLayout(progressRow_);
    progressLayout->setContentsMargins(4, 0, 4, 0);

    previousWindowAction_ = playbackToolbar_->addAction(style()->standardIcon(QStyle::SP_ArrowBack), QString());
    connect(previousWindowAction_, &QAction::triggered, this, [this]() {
        requestWindow(waveformWidget_->timeRange().first - viewState_.windowSeconds);
    });
    nextWindowAction_ = playbackToolbar_->addAction(style()->standardIcon(QStyle::SP_ArrowForward), QString());
    connect(nextWindowAction_, &QAction::triggered, this, [this]() {
        requestWindow(waveformWidget_->timeRange().first + viewState_.windowSeconds);
    });

    positionLabel_ = new QLabel(this);
    positionSpin_ = new QDoubleSpinBox(this);
    positionSpin_->setObjectName("eegPlaybackPosition");
    positionSpin_->setRange(0.0, 0.0);
    positionSpin_->setDecimals(3);
    positionSpin_->setSingleStep(1.0);
    positionSpin_->setSuffix(" s");
    positionSpin_->setMinimumWidth(100);
    playbackPanel_ = new EegPlaybackPanel(this);

    progressLayout->addWidget(positionLabel_);
    progressLayout->addWidget(positionSpin_);
    progressLayout->addWidget(playbackPanel_->progressSlider(), 1);
    connect(playbackPanel_->progressSlider(), &QSlider::sliderPressed, this, [this]() {
        syncVideoToPlayback();
    });
    connect(positionSpin_, &QDoubleSpinBox::editingFinished, this, [this]() {
        if (!refreshingBrowserControls_ && !openingFile_) {
            playback_.seek(positionSpin_->value());
            playbackElapsed_.restart();
            requestWindow(playback_.position());
            syncVideoToPlayback();
            refreshPlaybackControls();
        }
    });

    connect(playbackPanel_, &EegPlaybackPanel::progressPreviewed, this, [this](double fraction) {
        if (!hasCurrentFile_ || openingFile_) return;
        const QSignalBlocker blocker(positionSpin_);
        positionSpin_->setValue(playback_.duration() * fraction);
    });
    connect(playbackPanel_, &EegPlaybackPanel::progressMoved, this, [this](double fraction) {
        if (!hasCurrentFile_ || openingFile_) {
            return;
        }
        const double position = playback_.duration() * fraction;
        playback_.seek(position);
        playbackElapsed_.restart();
        requestWindow(position);
        syncVideoToPlayback();
        refreshPlaybackControls();
    });
    timeWindowLabel_ = new QLabel(this);
    timeWindowSpin_ = new QDoubleSpinBox(this);
    timeWindowSpin_->setRange(1.0, 120.0);
    timeWindowSpin_->setDecimals(0);
    timeWindowSpin_->setValue(viewState_.windowSeconds);
    timeWindowSpin_->setSuffix(" s");

    autoAmplitudeCheck_ = new QCheckBox(this);
    autoAmplitudeCheck_->setObjectName("eegAutoAmplitude");
    autoAmplitudeCheck_->setChecked(true);
    connect(autoAmplitudeCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        waveformWidget_->setAutoScaleEnabled(enabled);
        amplitudeSpin_->setEnabled(!enabled);
        applyBrowserStateToWaveform();
    });
    amplitudeLabel_ = new QLabel(this);
    amplitudeSpin_ = new QDoubleSpinBox(this);
    amplitudeSpin_->setObjectName("eegManualAmplitude");
    amplitudeSpin_->setEnabled(false);
    amplitudeSpin_->setRange(50.0, 5000.0);
    amplitudeSpin_->setDecimals(0);
    amplitudeSpin_->setValue(viewState_.amplitudeUv);
    amplitudeSpin_->setSuffix(" uV");

    firstChannelLabel_ = new QLabel(this);
    firstChannelSpin_ = new QSpinBox(this);
    firstChannelSpin_->setRange(0, 0);
    firstChannelSpin_->setValue(viewState_.firstVisibleChannel);

    browserToolbar_->addSeparator();
    browserToolbar_->addPair(timeWindowLabel_, timeWindowSpin_);
    browserToolbar_->addSeparator();
    browserToolbar_->addWidget(autoAmplitudeCheck_);
    browserToolbar_->addPair(amplitudeLabel_, amplitudeSpin_);
    browserToolbar_->addSeparator();
    browserToolbar_->addPair(firstChannelLabel_, firstChannelSpin_);

    channelDock_ = new QDockWidget(this);
    channelDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    channelScroll_ = new QScrollArea(channelDock_);
    channelScroll_->setWidgetResizable(true);
    channelListWidget_ = new QWidget(channelScroll_);
    channelListLayout_ = new QVBoxLayout(channelListWidget_);
    channelListLayout_->setContentsMargins(4, 4, 4, 4);
    channelListLayout_->setSpacing(2);
    channelScroll_->setWidget(channelListWidget_);
    channelPanel_ = new CollapsiblePanel(i18n_->text("app.browser.channels"), channelDock_);
    channelPanel_->addWidget(channelScroll_);
    channelDock_->setWidget(channelPanel_);
    channelDock_->hide();
    browserToolbar_->hide();

    connect(timeWindowSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (refreshingBrowserControls_) {
            return;
        }
        viewState_.setWindowSeconds(value);
        applyBrowserStateToWaveform();
        if (hasCurrentFile_ && windowRequestHandler_) {
            requestWindow(playback_.position());
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
    playAction_ = playbackToolbar_->addAction(style()->standardIcon(QStyle::SP_MediaPlay), QString());
    connect(playAction_, &QAction::triggered, this, &MainWindow::togglePlayback);
    playbackPanel_->buildInto(playbackToolbar_);
    connect(playbackPanel_, &EegPlaybackPanel::speedChanged, this, [this](double speed) {
        playback_.setSpeed(speed);
        if (videoPanel_ != nullptr) {
            videoPanel_->setRate(playback_.speed());
            syncVideoToPlayback();
        }
    });
    markerModeAction_ = browserToolbar_->addAction(QString());
    markerModeAction_->setCheckable(true);
    connect(markerModeAction_, &QAction::toggled, this, [this](bool enabled) { waveformWidget_->setMarkerModeEnabled(enabled); });
}

void MainWindow::buildRecordControls() {
    const QString root = repoRoot_.isEmpty() ? QStringLiteral(BF_REPO_ROOT) : repoRoot_;
    recordController_ = new RecordController(root, version_, this);
    exportController_ = new ExportController(root, version_, this);
    recordAction_ = playbackToolbar_->addAction(QString());
    exportSegmentAction_ = playbackToolbar_->addAction(QString());
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
    RecordMetadataDialog dialog(i18n_, recordOutputDir_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QJsonObject metadata = dialog.metadata();
    const QString outputDir = dialog.outputDirectory();
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
    auto* buttons = new ControlFlowLayout(nullptr);
    deleteMarkerButton_ = new QPushButton(content);
    clearMarkersButton_ = new QPushButton(content);
    exportMarkersButton_ = new QPushButton(content);
    buttons->addWidget(deleteMarkerButton_);
    buttons->addWidget(clearMarkersButton_);
    buttons->addWidget(exportMarkersButton_);
    layout->addLayout(buttons);
    markerPanel_ = new CollapsiblePanel(i18n_->text("app.marker.panel"), markerDock_);
    markerPanel_->addWidget(content);
    markerDock_->setWidget(markerPanel_);
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
    videoDock_->hide();
    connect(videoPanel_, &VideoPanel::offsetApplyRequested, this, [this]() {
        videoPanel_->applyOffset(playback_.position(), playback_.isPlaying() && !waitingForPlaybackData_
            && !playbackPanel_->isProgressSliderDown());
        updatePlaybackDuration();
        statusBar()->showMessage(i18n_->text(
            "app.video.offset_applied",
            {{"offset", i18n_->formatNumber(videoPanel_->offsetSeconds(), 3)}}),
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
    processingPanel_->buildInto(browserToolbar_, filterToolbar_, processingState_);
    connect(processingPanel_, &EegProcessingPanel::changed, this, &MainWindow::onProcessingUiChanged);
    connect(processingPanel_, &EegProcessingPanel::applyBandpassToSelected, this, &MainWindow::applyBandpassToSelectedChannels);
    connect(processingPanel_, &EegProcessingPanel::clearPerChannelBandpass, this, &MainWindow::clearPerChannelBandpassOverrides);
}

void MainWindow::buildAnalysisDock() {
    analysisDock_ = new QDockWidget(this);
    analysisDock_->setAllowedAreas(Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    analysisTabs_ = new QTabWidget(analysisDock_);
    analysisTabs_->setObjectName("eegAnalysisTabs");
    analysisDock_->setWidget(analysisTabs_);
    analysisDock_->hide();

    const auto addHeader = [this](QWidget* page, QVBoxLayout* layout, int index) {
        auto* row = new QHBoxLayout;
        auto* title = new QLabel(page);
        title->setObjectName(QString("eegAnalysisTitle_%1").arg(index));
        title->setTextFormat(Qt::PlainText);
        QFont font = title->font();
        font.setBold(true);
        title->setFont(font);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto* status = new QLabel(page);
        status->setObjectName(QString("eegAnalysisStatus_%1").arg(index));
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        row->addWidget(title, 1);
        layout->addLayout(row);
        analysisTitleLabels_[index] = title;
        analysisStatusLabels_[index] = status;
    };
    const auto addFooter = [this](QWidget* page, QVBoxLayout* layout, int index, QLabel* summary = nullptr) {
        // Properties have a bounded area outside the graph. Long translations,
        // detailed errors and narrow windows can scroll without shrinking the
        // graph or pushing the common analysis controls out of the workspace.
        auto* scroll = new QScrollArea(page);
        scroll->setObjectName(QString("eegAnalysisProperties_%1").arg(index));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setMinimumHeight(48);
        scroll->setMaximumHeight(128);
        scroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        auto* content = new QWidget(scroll);
        auto* properties = new QVBoxLayout(content);
        properties->setContentsMargins(2, 0, 2, 0);
        properties->setSpacing(2);
        properties->setSizeConstraint(QLayout::SetMinAndMaxSize);
        properties->addWidget(analysisStatusLabels_[index]);
        auto* legend = new QLabel(content);
        legend->setObjectName(QString("eegAnalysisLegend_%1").arg(index));
        legend->setWordWrap(true);
        legend->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
        properties->addWidget(legend);
        analysisLegendLabels_[index] = legend;
        if (summary == nullptr) summary = new QLabel(content);
        summary->setObjectName(QString("eegAnalysisSummary_%1").arg(index));
        summary->setTextFormat(Qt::PlainText);
        summary->setWordWrap(true);
        summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
        properties->addWidget(summary);
        analysisSummaryLabels_[index] = summary;
        properties->addStretch();
        scroll->setWidget(content);
        layout->addWidget(scroll);
    };
    auto* psdPage = new QWidget(analysisTabs_);
    auto* psdLayout = new QVBoxLayout(psdPage);
    addHeader(psdPage, psdLayout, 0);
    auto* psdControls = new ControlFlowLayout(nullptr);
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
    spectrumWidget_ = new SpectrumWidget(psdPage);
    spectrumWidget_->setObjectName("eegAnalysisPlot_0");
    spectrumWidget_->setMinimumSize(220, 160);
    spectrumWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    psdLayout->addLayout(psdControls);
    psdLayout->addWidget(spectrumWidget_, 1);
    psdBandSummaryLabel_ = new QLabel(psdPage);
    psdBandSummaryLabel_->setWordWrap(true);
    psdBandSummaryLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    addFooter(psdPage, psdLayout, 0, psdBandSummaryLabel_);
    analysisTabs_->addTab(psdPage, QString());

    auto* stftPage = new QWidget(analysisTabs_);
    auto* stftLayout = new QVBoxLayout(stftPage);
    addHeader(stftPage, stftLayout, 1);
    auto* stftControls = new ControlFlowLayout(nullptr);
    stftChannelCombo_ = new QComboBox(stftPage);
    stftResolutionCombo_ = new QComboBox(stftPage);
    stftFmaxSpin_ = new QSpinBox(stftPage);
    stftFmaxSpin_->setRange(5, 500);
    stftFmaxSpin_->setValue(100);
    stftFmaxSpin_->setSuffix(" Hz");
    stftControls->addWidget(stftChannelCombo_);
    stftControls->addWidget(stftResolutionCombo_);
    stftControls->addWidget(stftFmaxSpin_);
    spectrogramWidget_ = new SpectrogramWidget(stftPage);
    spectrogramWidget_->setObjectName("eegAnalysisPlot_1");
    spectrogramWidget_->setMinimumSize(220, 160);
    spectrogramWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    stftLayout->addLayout(stftControls);
    stftLayout->addWidget(spectrogramWidget_, 1);
    addFooter(stftPage, stftLayout, 1);
    analysisTabs_->addTab(stftPage, QString());

    auto* trendPage = new QWidget(analysisTabs_);
    stftResolutionCombo_->addItem(QString());
    stftResolutionCombo_->addItem(QString());
    stftResolutionCombo_->addItem(QString());
    auto* trendLayout = new QVBoxLayout(trendPage);
    addHeader(trendPage, trendLayout, 2);
    auto* trendControls = new ControlFlowLayout(nullptr);
    trendChannelCombo_ = new QComboBox(trendPage);
    trendEpochSpin_ = new QDoubleSpinBox(trendPage);
    trendEpochSpin_->setRange(0.5, 30.0);
    trendEpochSpin_->setValue(1.0);
    trendEpochSpin_->setSuffix(" s");
    trendControls->addWidget(trendChannelCombo_);
    trendControls->addWidget(trendEpochSpin_);
    trendWidget_ = new BandTrendWidget(trendPage);
    trendWidget_->setObjectName("eegAnalysisPlot_2");
    trendWidget_->setMinimumSize(220, 160);
    trendWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    trendLayout->addLayout(trendControls);
    trendLayout->addWidget(trendWidget_, 1);
    addFooter(trendPage, trendLayout, 2);
    analysisTabs_->addTab(trendPage, QString());

    auto* coherencePage = new QWidget(analysisTabs_);
    auto* coherenceLayout = new QVBoxLayout(coherencePage);
    addHeader(coherencePage, coherenceLayout, 3);
    auto* coherenceControls = new ControlFlowLayout(nullptr);
    coherenceACombo_ = new QComboBox(coherencePage);
    coherenceBCombo_ = new QComboBox(coherencePage);
    coherenceFmaxSpin_ = new QSpinBox(coherencePage);
    coherenceFmaxSpin_->setRange(5, 500);
    coherenceFmaxSpin_->setValue(100);
    coherenceFmaxSpin_->setSuffix(" Hz");
    coherenceControls->addWidget(coherenceACombo_);
    coherenceControls->addWidget(coherenceBCombo_);
    coherenceControls->addWidget(coherenceFmaxSpin_);
    coherenceWidget_ = new SpectrumWidget(coherencePage);
    coherenceWidget_->setObjectName("eegAnalysisPlot_3");
    coherenceWidget_->setMinimumSize(220, 160);
    coherenceWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    coherenceSummaryLabel_ = new QLabel(coherencePage);
    coherenceSummaryLabel_->setWordWrap(true);
    coherenceSummaryLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    coherenceLayout->addLayout(coherenceControls);
    coherenceLayout->addWidget(coherenceWidget_, 1);
    addFooter(coherencePage, coherenceLayout, 3, coherenceSummaryLabel_);
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

void MainWindow::requestCurrentAnalysis(bool background) {
    if (!hasCurrentFile_ || refreshingAnalysisControls_ || analysisService_ == nullptr) {
        return;
    }
    const int currentTab = analysisTabs_->currentIndex();
    if (currentTab < 0 || currentTab >= 4) return;
    // Automatic playback updates must let an expensive analysis finish. User
    // changes still supersede work immediately via a new generation.
    if (background && requestedAnalysisPage_ == currentTab
        && analysisPages_[currentTab].state == AnalysisDisplayState::Pending) return;
    AnalysisRequest request;
    request.fs = currentFile_.waveform.fs;
    if (!(request.fs > 0.0) || windowRequestPending_ || openingFile_) return;
    const auto range = waveformWidget_->timeRange();
    const double start = range.first;
    const double end = range.second;
    const double firstSample = std::max(0.0, std::ceil((start - currentFile_.waveform.t0) * request.fs - 1e-6));
    const double endSample = std::max(firstSample, std::ceil((end - currentFile_.waveform.t0) * request.fs - 1e-6));
    for (const auto& channel : currentFile_.waveform.data) {
        const auto first = static_cast<std::size_t>(std::min(firstSample, static_cast<double>(channel.size())));
        const auto last = static_cast<std::size_t>(std::min(endSample, static_cast<double>(channel.size())));
        request.data.emplace_back(channel.begin() + first, channel.begin() + last);
    }
    request.tStart = currentFile_.waveform.t0 + firstSample / request.fs;
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
    // Every request owns one page and its exact input context. A pending result
    // from a previously selected page must never repopulate a different page.
    if (requestedAnalysisPage_ >= 0 && requestedAnalysisPage_ != currentTab
        && analysisPages_[requestedAnalysisPage_].state == AnalysisDisplayState::Pending) {
        analysisPages_[requestedAnalysisPage_] = {};
        analysisStatusLabels_[requestedAnalysisPage_]->setProperty("analysisGeneration", 0);
        clearAnalysisPlot(requestedAnalysisPage_);
        refreshAnalysisPage(requestedAnalysisPage_);
    }
    auto& page = analysisPages_[currentTab];
    page = {};
    page.state = AnalysisDisplayState::Pending;
    page.kind = request.kind;
    page.start = request.tStart;
    page.end = request.tStart + (request.data.empty() ? 0.0 : request.data.front().size() / request.fs);
    page.fmax = request.fmax;
    page.asDb = request.kind == AnalysisKind::Psd && psdDbCheck_->isChecked();
    page.channel = currentFile_.waveform.channelNames.value(request.channel);
    if (request.kind == AnalysisKind::Coherence) {
        page.channel += QStringLiteral(" / ") + currentFile_.waveform.channelNames.value(request.channelB);
    }
    requestedAnalysisPage_ = currentTab;
    clearAnalysisPlot(currentTab);
    refreshAnalysisPage(currentTab);
    analysisGeneration_ = analysisService_->request(request);
    analysisStatusLabels_[currentTab]->setProperty("analysisGeneration", analysisGeneration_);
    analysisElapsed_.restart();
}

void MainWindow::invalidateAnalysis() {
    if (analysisService_ != nullptr) analysisService_->cancel();
    analysisGeneration_ = 0;
    requestedAnalysisPage_ = -1;
    for (int index = 0; index < 4; ++index) {
        analysisPages_[index] = {};
        if (analysisStatusLabels_[index] != nullptr) analysisStatusLabels_[index]->setProperty("analysisGeneration", 0);
        clearAnalysisPlot(index);
        refreshAnalysisPage(index);
    }
}

void MainWindow::clearAnalysisPlot(int page) {
    if (page == 0) spectrumWidget_->setSpectrum({}, {}, {});
    else if (page == 1) spectrogramWidget_->setSpectrogram({});
    else if (page == 2) trendWidget_->setTrend({}, {});
    else coherenceWidget_->setSpectrum({}, {}, {}, true);
    if (analysisSummaryLabels_[page] != nullptr) analysisSummaryLabels_[page]->clear();
}

void MainWindow::refreshAnalysisPage(int index) {
    if (analysisStatusLabels_[index] == nullptr) return;
    const auto& page = analysisPages_[index];
    const QString titleKey = index == 0
        ? (page.kind == AnalysisKind::Fft ? "app.analysis.fft" : "app.analysis.psd")
        : (index == 1 ? "app.analysis.stft" : (index == 2 ? "app.analysis.trend" : "app.analysis.coherence"));
    analysisTitleLabels_[index]->setText(i18n_->text(titleKey));
    QString stateText;
    switch (page.state) {
    case AnalysisDisplayState::Idle:
    case AnalysisDisplayState::Empty: stateText = i18n_->text("app.analysis.empty"); break;
    case AnalysisDisplayState::Pending: stateText = i18n_->text("app.analysis.pending"); break;
    case AnalysisDisplayState::Failed:
        stateText = i18n_->text("app.analysis.failed", {{"reason", i18n_->text(QString("error.%1").arg(page.errorCode))}});
        break;
    case AnalysisDisplayState::Ready:
        stateText = i18n_->text("app.analysis.ready");
        break;
    }
    const QString context = page.state == AnalysisDisplayState::Idle ? QString() : i18n_->text("app.analysis.context", {
        {"channel", page.channel}, {"start", i18n_->formatNumber(page.start, 2)}, {"end", i18n_->formatNumber(page.end, 2)},
    });
    analysisStatusLabels_[index]->setText(stateText + (context.isEmpty() ? QString() : QStringLiteral("\n") + context));
    analysisStatusLabels_[index]->setToolTip(analysisStatusLabels_[index]->text());
    const char* states[] = {"idle", "pending", "ready", "empty", "failed"};
    analysisStatusLabels_[index]->setProperty("analysisState", states[static_cast<int>(page.state)]);
    // Legends are ordinary wrapping labels outside the graph. Their colors use
    // the exact EEG-band colors used by the plot widgets.
    QStringList bands;
    if (index != 1 && !(index == 0 && page.kind == AnalysisKind::Fft)) {
        for (const auto& band : dsp::eegBands()) {
            const QString name = i18n_->text("app.analysis.band_legend", {
                {"band", QString::fromLatin1(band.key)}, {"low", i18n_->formatNumber(band.low, 1)},
                {"high", i18n_->formatNumber(band.high, 1)},
            });
            const QColor color(band.rgba[0], band.rgba[1], band.rgba[2]);
            bands.push_back(QString("<span style='background-color:%1'>&nbsp;&nbsp;</span> %2").arg(color.name(), name.toHtmlEscaped()));
        }
    }
    analysisLegendLabels_[index]->setText(bands.join(QStringLiteral(" &nbsp; ")));
    analysisLegendLabels_[index]->setVisible(!bands.isEmpty());
    QString yLabel;
    if (index == 0) yLabel = i18n_->text(page.kind == AnalysisKind::Fft ? "app.analysis.amplitude" : (page.asDb ? "app.analysis.db_unit" : "app.analysis.psd_unit"));
    else if (index == 1) yLabel = i18n_->text("app.analysis.frequency");
    else if (index == 2) yLabel = i18n_->text("app.analysis.trend_unit");
    else yLabel = i18n_->text("app.analysis.coherence");
    const QString empty = QString();
    if (index == 0) spectrumWidget_->setAxisLabels(i18n_->text("app.analysis.frequency"), yLabel, empty);
    else if (index == 1) spectrogramWidget_->setLabels(i18n_->text(titleKey), i18n_->text("app.analysis.time"), yLabel, empty);
    else if (index == 2) trendWidget_->setAxisLabels(i18n_->text("app.analysis.time"), yLabel, empty);
    else coherenceWidget_->setAxisLabels(i18n_->text("app.analysis.frequency"), yLabel, empty);
    const std::array<QWidget*, 4> plots{spectrumWidget_, spectrogramWidget_, trendWidget_, coherenceWidget_};
    plots[index]->setLocale(i18n_->qlocale());
}

void MainWindow::handleAnalysisCompleted(int generation, const AnalysisResult& result) {
    if (generation != analysisGeneration_ || requestedAnalysisPage_ < 0) return;
    auto& page = analysisPages_[requestedAnalysisPage_];
    if (page.state != AnalysisDisplayState::Pending || result.kind != page.kind) return;
    const auto finiteVector = [](const std::vector<double>& values) {
        return !values.empty() && std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
    };
    bool valid = false;
    switch (result.kind) {
    case AnalysisKind::Psd:
    case AnalysisKind::Fft:
    case AnalysisKind::Coherence:
        valid = finiteVector(result.spectrum.frequencies) && finiteVector(result.spectrum.values)
            && result.spectrum.frequencies.size() == result.spectrum.values.size();
        break;
    case AnalysisKind::Stft:
        valid = finiteVector(result.spectrogram.times) && finiteVector(result.spectrogram.frequencies)
            && result.spectrogram.values.size() == result.spectrogram.frequencies.size()
            && std::all_of(result.spectrogram.values.begin(), result.spectrogram.values.end(), [&](const auto& row) {
                return row.size() == result.spectrogram.times.size() && finiteVector(row);
            });
        break;
    case AnalysisKind::BandTrend:
        valid = finiteVector(result.trend.times) && result.trend.powers.size() == dsp::eegBands().size()
            && std::all_of(result.trend.powers.begin(), result.trend.powers.end(), [&](const auto& row) {
                return row.size() == result.trend.times.size() && finiteVector(row);
            });
        break;
    }
    page.state = valid ? AnalysisDisplayState::Ready : AnalysisDisplayState::Empty;
    if (valid) { page.result = result; renderAnalysisResult(requestedAnalysisPage_); }
    else { page.result = {}; clearAnalysisPlot(requestedAnalysisPage_); }
    refreshAnalysisPage(requestedAnalysisPage_);
}

void MainWindow::renderAnalysisResult(int index) {
    const auto& page = analysisPages_[index];
    if (page.state != AnalysisDisplayState::Ready) return;
    const auto& result = page.result;
    switch (result.kind) {
    case AnalysisKind::Psd:
    case AnalysisKind::Fft: {
        const bool fft = result.kind == AnalysisKind::Fft;
        const bool asDb = !fft && page.asDb;
        dsp::Spectrum spectrum = result.spectrum;
        if (asDb) {
            for (double& value : spectrum.values) {
                value = 10.0 * std::log10(std::max(value, 1e-12));
            }
        }
        const auto limit = std::upper_bound(spectrum.frequencies.begin(), spectrum.frequencies.end(), page.fmax);
        const std::size_t keep = static_cast<std::size_t>(limit - spectrum.frequencies.begin());
        spectrum.frequencies.resize(keep);
        spectrum.values.resize(keep);
        spectrumWidget_->setSpectrum(
            spectrum,
            fft ? i18n_->text("app.analysis.fft") : i18n_->text("app.analysis.psd"),
            fft ? i18n_->text("app.analysis.amplitude") : (asDb ? i18n_->text("app.analysis.db_unit") : i18n_->text("app.analysis.psd_unit")));
        if (!fft && !asDb) {
            QStringList powers;
            for (const dsp::Band& band : dsp::eegBands()) {
                powers.push_back(QString("%1=%2").arg(band.key).arg(i18n_->formatNumber(dsp::bandPower(result.spectrum, band.low, band.high), 2)));
            }
            psdBandSummaryLabel_->setText(i18n_->text("app.analysis.trend_unit") + QStringLiteral(": ") + powers.join(" | "));
        } else {
            psdBandSummaryLabel_->clear();
        }
        break;
    }
    case AnalysisKind::Stft: {
        spectrogramWidget_->setSpectrogram(result.spectrogram);
        double low = result.spectrogram.values.front().front();
        double high = low;
        for (const auto& row : result.spectrogram.values) {
            for (double value : row) { low = std::min(low, value); high = std::max(high, value); }
        }
        analysisSummaryLabels_[1]->setText(i18n_->text("app.analysis.color_range", {
            {"min", i18n_->formatNumber(low, 1)}, {"max", i18n_->formatNumber(high, 1)},
        }));
        break;
    }
    case AnalysisKind::BandTrend:
        trendWidget_->setTrend(result.trend.times, result.trend.powers);
        analysisSummaryLabels_[2]->setText(i18n_->text("app.analysis.trend_unit"));
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
            values.push_back(QString("%1=%2").arg(band.key).arg(i18n_->formatNumber(count > 0 ? sum / count : 0.0, 2)));
        }
        coherenceSummaryLabel_->setText(values.join(" | "));
        break;
    }
    }
}

double MainWindow::viewportStart(double position) const {
    return std::clamp(position, 0.0, std::max(0.0, signalDurationSeconds_ - viewState_.windowSeconds));
}

bool MainWindow::cacheCoversPosition(double position) const {
    const double start = viewportStart(position);
    const double end = std::min(signalDurationSeconds_, start + viewState_.windowSeconds);
    const double tolerance = rawWaveform_.fs > 0.0 ? 1.0 / rawWaveform_.fs : 1e-6;
    return !rawWaveform_.data.empty() && rawWaveform_.t0 <= start + tolerance && rawWaveform_.t1 + tolerance >= end;
}

void MainWindow::updatePlaybackViewport() {
    if (!hasCurrentFile_) return;
    if (!playbackViewportActive_) {
        waveformWidget_->clearTimeRange();
        return;
    }
    const double start = viewportStart(playback_.position());
    waveformWidget_->setTimeRange(start, std::min(signalDurationSeconds_, start + viewState_.windowSeconds));
}

void MainWindow::requestPlaybackPrefetch() {
    if (!hasCurrentFile_ || openingFile_ || !windowRequestHandler_ || playbackPrefetchPending_ || windowRequestPending_) return;
    const double lead = std::clamp(viewState_.windowSeconds * 0.5, 2.0, 8.0);
    const double start = viewportStart(playback_.position());
    const double length = std::min(viewState_.windowSeconds + lead, signalDurationSeconds_ - start);
    if (length <= 0.0) return;
    playbackPrefetchPending_ = true;
    playbackPrefetchEnd_ = start + length;
    windowRequestHandler_(currentFile_.session.path, start, length);
}

void MainWindow::togglePlayback() {
    if (!hasCurrentFile_ || openingFile_) return;
    if (playback_.isPlaying()) {
        playback_.pause();
        playbackTimer_.stop();
        requestCurrentAnalysis();
    } else {
        playbackViewportActive_ = true;
        playback_.play();
        waitingForPlaybackData_ = windowRequestPending_ || !cacheCoversPosition(playback_.position());
        updatePlaybackViewport();
        // Fill the lead immediately, before the first frame leaves the
        // initial static window. A pending seek already owns the next result.
        if (rawWaveform_.t1 < signalDurationSeconds_ || waitingForPlaybackData_) requestPlaybackPrefetch();
        playbackElapsed_.restart();
        playbackTimer_.start();
    }
    syncVideoToPlayback();
    refreshPlaybackControls();
    refreshPersistentStatus();
}

void MainWindow::advancePlayback() {
    const double elapsedSeconds = playbackElapsed_.restart() / 1000.0;
    if (playbackPanel_ != nullptr && playbackPanel_->isProgressSliderDown()) return;
    if (waitingForPlaybackData_) {
        syncVideoToPlayback();
        return;
    }
    // Check the next view before advancing the clock. Slow IO cannot make
    // the view run off the available data or create a catch-up jump later.
    const double nextPosition = playback_.position() + elapsedSeconds * playback_.speed();
    if (playback_.isPlaying() && nextPosition < playback_.duration() && !cacheCoversPosition(nextPosition)) {
        waitingForPlaybackData_ = true;
        requestPlaybackPrefetch();
        syncVideoToPlayback();
        return;
    }
    const bool wasPlaying = playback_.isPlaying();
    playback_.advance(elapsedSeconds);
    updatePlaybackViewport();
    if (wasPlaying && !playback_.isPlaying() && playback_.position() == 0.0) {
        requestWindow(0.0);
    } else if (playback_.isPlaying()) {
        const double lead = std::clamp(viewState_.windowSeconds * 0.5, 2.0, 8.0);
        const double end = std::min(signalDurationSeconds_, viewportStart(playback_.position()) + viewState_.windowSeconds);
        if (rawWaveform_.t1 < signalDurationSeconds_ && rawWaveform_.t1 - end <= lead * 0.5) requestPlaybackPrefetch();
        if (!analysisElapsed_.isValid() || analysisElapsed_.elapsed() >= 500) requestCurrentAnalysis(true);
    }
    if (!playback_.isPlaying()) playbackTimer_.stop();
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
    if (positionSpin_ != nullptr && !playbackPanel_->isProgressSliderDown()) {
        const QSignalBlocker blocker(positionSpin_);
        positionSpin_->setValue(std::min(playback_.position(), positionSpin_->maximum()));
    }
    if (playbackPanel_ != nullptr) {
        const double fraction = playback_.duration() > 0.0
            ? playback_.position() / playback_.duration()
            : 0.0;
        playbackPanel_->setProgressFraction(fraction);
    }
}

void MainWindow::syncVideoToPlayback() {
    if (videoPanel_ != nullptr && videoPanel_->isLoaded()) {
        const bool scrubbing = playbackPanel_ != nullptr && playbackPanel_->isProgressSliderDown();
        videoPanel_->syncTo(playback_.position(), playback_.isPlaying() && !scrubbing && !waitingForPlaybackData_);
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
    const QString label = QInputDialog::getText(this, i18n_->text("app.marker.add_title"), i18n_->text("app.marker.add_prompt", {{"time", i18n_->formatNumber(time, 3)}}), QLineEdit::Normal, {}, &accepted);
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
        auto* time = new QTableWidgetItem(i18n_->formatNumber(marker.time, 3));
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
    if (generation != analysisGeneration_ || requestedAnalysisPage_ < 0) return;
    auto& page = analysisPages_[requestedAnalysisPage_];
    page.state = AnalysisDisplayState::Failed;
    page.errorCode = code;
    page.result = {};
    clearAnalysisPlot(requestedAnalysisPage_);
    refreshAnalysisPage(requestedAnalysisPage_);
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
    if (!hasCurrentFile_ || openingFile_ || !windowRequestHandler_) return;
    if (!playback_.isPlaying()) playbackViewportActive_ = false;
    playback_.seek(t0);
    playbackElapsed_.restart();
    playbackPrefetchPending_ = false;
    windowRequestPending_ = true;
    waitingForPlaybackData_ = playback_.isPlaying();
    invalidateAnalysis();
    updatePlaybackViewport();
    syncVideoToPlayback();
    statusKey_ = "app.status.file_opening";
    statusParams_ = {{"path", currentFile_.session.path}};
    windowRequestHandler_(currentFile_.session.path, viewportStart(playback_.position()), viewState_.windowSeconds);
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
        if (!playbackPanel_->isProgressSliderDown()) {
            positionSpin_->setValue(playback_.position());
        }
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

    QStringList names;
    for (int channel = 0; channel < channelCount(); ++channel) {
        names.push_back(channel < currentFile_.waveform.channelNames.size()
            ? currentFile_.waveform.channelNames[channel] : QString("Ch%1").arg(channel + 1));
    }
    if (channelBandpassControls_ != nullptr) {
        channelBandpassControls_->refresh(names, hasCurrentFile_ ? currentFile_.waveform.fs : 0.0, processingState_);
    }
    if (names == channelControlNames_ && channelChecks_.size() == names.size()) {
        for (int channel = 0; channel < channelChecks_.size(); ++channel) {
            const QSignalBlocker blocker(channelChecks_[channel]);
            channelChecks_[channel]->setChecked(viewState_.isChannelVisible(channel));
        }
        return;
    }
    channelControlNames_ = names;
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
        const QString label = names[channel];
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
    const QScopedValueRollback<bool> analysisTranslationGuard(refreshingAnalysisControls_, true);
    setWindowTitle(i18n_->text("app.window_title", {{"product_name", version_.productName}, {"version", version_.version}}));
    centralLabel_->setText(i18n_->text(emptyStateKey_, emptyStateParams_));
    fileMenu_->setTitle(i18n_->text("app.menu.file"));
    fileMenu_->menuAction()->setText(i18n_->text("app.menu.file"));
    openAction_->setText(i18n_->text("app.action.open"));
    openVideoAction_->setText(i18n_->text("app.video.open"));
    openButton_->setText(i18n_->text("app.action.open"));
    quitAction_->setText(i18n_->text("app.action.quit"));
    languageMenu_->setTitle(i18n_->text("app.menu.language"));
    languageMenu_->menuAction()->setText(i18n_->text("app.menu.language"));
    viewMenu_->setTitle(i18n_->text("app.menu.view"));
    viewMenu_->menuAction()->setText(i18n_->text("app.menu.view"));
    toggleChannelsAction_->setText(i18n_->text("app.view.channels"));
    toggleMarkersAction_->setText(i18n_->text("app.view.markers"));
    toggleAnalysisAction_->setText(i18n_->text("app.view.analysis"));
    toggleVideoAction_->setText(i18n_->text("app.view.video"));
    themeMenu_->setTitle(i18n_->text("app.menu.theme"));
    themeMenu_->menuAction()->setText(i18n_->text("app.menu.theme"));
    lightThemeAction_->setText(i18n_->text("app.theme.light"));
    darkThemeAction_->setText(i18n_->text("app.theme.dark"));
    for (QAction* action : languageActions_) {
        const QString locale = action->data().toString();
        action->setText(i18n_->text(QString("app.language.%1").arg(locale)));
        action->setChecked(locale == i18n_->locale());
    }

    previousWindowAction_->setText(i18n_->text("app.browser.previous"));
    previousWindowAction_->setToolTip(i18n_->text("app.browser.previous"));
    nextWindowAction_->setText(i18n_->text("app.browser.next"));
    nextWindowAction_->setToolTip(i18n_->text("app.browser.next"));
    positionLabel_->setText(i18n_->text("app.browser.position"));
    timeWindowLabel_->setText(i18n_->text("app.browser.window"));
    autoAmplitudeCheck_->setText(i18n_->text("app.browser.auto_amplitude"));
    amplitudeLabel_->setText(i18n_->text("app.browser.amplitude"));
    firstChannelLabel_->setText(i18n_->text("app.browser.first_channel"));
    waveformWidget_->setAxisLabels(i18n_->text("app.analysis.time"), i18n_->text("app.analysis.amplitude"));
    waveformWidget_->setNumberLocale(i18n_->qlocale());
    channelDock_->setWindowTitle(i18n_->text("app.browser.channels"));
    channelPanel_->setTitle(i18n_->text("app.browser.channels"));
    if (processingPanel_ != nullptr) {
        processingPanel_->retranslateUi(i18n_);
        if (channelBandpassControls_ != nullptr) channelBandpassControls_->retranslate();
        if (filterPanel_) filterPanel_->setTitle(i18n_->text("app.processing.panel"));
    }
    analysisDock_->setWindowTitle(i18n_->text("app.analysis.panel"));
    analysisTabs_->setTabText(0, i18n_->text("app.analysis.psd"));
    analysisTabs_->setTabText(1, i18n_->text("app.analysis.stft"));
    analysisTabs_->setTabText(2, i18n_->text("app.analysis.trend"));
    for (int page = 0; page < 4; ++page) {
        renderAnalysisResult(page);
        refreshAnalysisPage(page);
    }
    analysisTabs_->setTabText(3, i18n_->text("app.analysis.coherence"));
    psdModeCombo_->setItemText(0, i18n_->text("app.analysis.welch"));
    psdModeCombo_->setItemText(1, i18n_->text("app.analysis.fft"));
    psdDbCheck_->setText(i18n_->text("app.analysis.db_display"));
    stftResolutionCombo_->setItemText(0, i18n_->text("app.analysis.high_time"));
    stftResolutionCombo_->setItemText(1, i18n_->text("app.analysis.balanced"));
    stftResolutionCombo_->setItemText(2, i18n_->text("app.analysis.high_frequency"));
    markerModeAction_->setText(i18n_->text("app.marker.mode"));
    markerModeAction_->setToolTip(i18n_->text("app.marker.mode_tooltip"));
    playAction_->setText(i18n_->text(playback_.isPlaying() ? "app.playback.pause" : "app.playback.play"));
    playAction_->setToolTip(playAction_->text());
    if (playbackPanel_ != nullptr) {
        playbackPanel_->retranslateUi(i18n_);
    }
    markerDock_->setWindowTitle(i18n_->text("app.marker.panel"));
    markerPanel_->setTitle(i18n_->text("app.marker.panel"));
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
    openingFile_ = true;
    playback_.pause();
    playbackTimer_.stop();
    playbackPrefetchPending_ = false;
    windowRequestPending_ = false;
    waitingForPlaybackData_ = false;
    invalidateAnalysis();
    syncVideoToPlayback();
    setLoading(true);
    statusKey_ = "app.status.file_opening";
    statusParams_ = {{"path", path}};
    retranslateUi();
}

void MainWindow::showFileOpenResult(const FileOpenResult& result) {
    setLoading(false);
    const bool sameSession = !openingFile_ && hasCurrentFile_ && currentFile_.session.path == result.session.path;
    const bool prefetched = sameSession && playbackPrefetchPending_;
    const double tolerance = result.waveform.fs > 0.0 ? 1.0 / result.waveform.fs : 1e-6;
    if (prefetched && (result.waveform.data.empty()
        || result.waveform.t0 > viewportStart(playback_.position()) + tolerance
        || result.waveform.t1 + tolerance < playbackPrefetchEnd_)) {
        // A successful but short response must not start an endless sequence
        // of identical prefetches. Keep the last usable display and stop.
        showFileOpenError(makeError("reader.slice_out_of_range", {
            {"handle", result.session.handle}, {"t0", result.waveform.t0},
            {"t1", result.waveform.t1}, {"duration", signalDurationSeconds_},
        }));
        return;
    }
    openingFile_ = false;
    currentFile_ = result;
    rawWaveform_ = result.waveform;
    hasCurrentFile_ = true;
    if (!sameSession && recordController_ != nullptr && recordController_->isActive()) {
        recordController_->stop();
    }
    if (!sameSession) {
        // Overrides belong to this file's output-channel identities, not a
        // coincidentally matching index in the next file (or sample rate).
        processingState_.clearAllChannelBandpass();
        if (channelBandpassControls_ != nullptr) channelBandpassControls_->refresh({}, 0.0, processingState_);
    }
    playbackPrefetchPending_ = false;
    windowRequestPending_ = false;
    waitingForPlaybackData_ = false;
    playbackElapsed_.restart();
    if (prefetched) {
        currentFile_.waveform = applyProcessingToSlice(rawWaveform_, processingState_);
        waveformWidget_->setSlice(currentFile_.waveform);
        updatePlaybackViewport();
        if (!playback_.isPlaying() || !analysisElapsed_.isValid() || analysisElapsed_.elapsed() >= 500) requestCurrentAnalysis(playback_.isPlaying());
        playbackElapsed_.restart();
        syncVideoToPlayback();
        return;
    }
    centralStack_->setCurrentWidget(waveformWidget_);
    browserToolbar_->show();
    playbackBar_->show();
    if (!sameSession) {
        playbackViewportActive_ = false;
        waveformWidget_->clearTimeRange();
        const QSignalBlocker blocker(playbackPanel_->progressSlider());
        playbackPanel_->progressSlider()->setSliderDown(false);
        playback_.pause();
        playbackTimer_.stop();
        signalDurationSeconds_ = result.session.duration;
        updatePlaybackDuration();
        playback_.seek(result.waveform.t0);
        const QSignalBlocker positionBlocker(positionSpin_);
        positionSpin_->setRange(0.0, playback_.duration());
        positionSpin_->setValue(playback_.position());
        QVector<QPair<double, QString>> events;
        events.reserve(result.session.events.size());
        for (const SidecarEvent& event : result.session.events) {
            events.push_back({event.time, event.label});
        }
        markerStore_.replaceFromEvents(events);
    }
    if (!sameSession) channelDock_->show();
    refreshBrowserControls();
    refreshProcessingControls();
    applyProcessingAndDisplay();
    refreshAnalysisControls();
    if (!sameSession) analysisDock_->show();
    refreshPersistentStatus();
    if (!sameSession) markerDock_->show();
    refreshPlaybackControls();
    refreshMarkerControls();
    playbackElapsed_.restart();
    syncVideoToPlayback();
}

void MainWindow::showFileOpenError(const AppError& error) {
    setLoading(false);
    playbackPrefetchPending_ = false;
    windowRequestPending_ = false;
    waitingForPlaybackData_ = false;
    openingFile_ = false;
    playback_.pause();
    playbackTimer_.stop();
    syncVideoToPlayback();
    refreshPlaybackControls();
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
    updatePlaybackViewport();

    const WaveformDisplayModel model = waveformDisplayModel(currentFile_.waveform, viewState_, 0);
    statusKey_ = waveformWidget_->autoScaleEnabled() ? "app.status.file_window_auto" : "app.status.file_window";
    statusParams_ = {
        {"format", currentFile_.session.format},
        {"t0", waveformWidget_->timeRange().first},
        {"t1", waveformWidget_->timeRange().second},
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

void MainWindow::applyBandpassToSelectedChannels() {
    // Write the current global bandpass (enabled/low/high) as a per-channel
    // override onto every visible channel. Channels not visible keep whatever
    // override they had (or fall back to global). This keeps the global knobs
    // as the default while letting the user specialize specific channels.
    const ChannelBandpassUi setting{
        processingState_.globalBandpassEnabled,
        processingState_.bandpassLow,
        processingState_.bandpassHigh,
    };
    const int nChannels = channelCount();
    for (int ch = 0; ch < nChannels; ++ch) {
        if (viewState_.isChannelVisible(ch)) {
            processingState_.setChannelBandpass(ch, setting);
        }
    }
    refreshProcessingControls();
    applyProcessingAndDisplay();
}

void MainWindow::clearPerChannelBandpassOverrides() {
    processingState_.clearAllChannelBandpass();
    if (channelBandpassControls_) channelBandpassControls_->clearDrafts();
    refreshProcessingControls();
    applyProcessingAndDisplay();
}

void MainWindow::applyProcessingAndDisplay() {
    if (!hasCurrentFile_) {
        return;
    }
    invalidateAnalysis();
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
