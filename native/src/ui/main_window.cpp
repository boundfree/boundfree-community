#include "ui/main_window.h"

#include "controller/processing_view_state.h"
#include "render/waveform_slice.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeySequence>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QMenuBar>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace bf {

MainWindow::MainWindow(const VersionInfo& version, I18nService* i18n, QWidget* parent)
    : QMainWindow(parent), version_(version), i18n_(i18n) {
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
    connect(openButton_, &QPushButton::clicked, this, &MainWindow::chooseFile);
    resize(900, 560);
    retranslateUi();
}

void MainWindow::buildFileMenu() {
    fileMenu_ = menuBar()->addMenu(QString());
    openAction_ = fileMenu_->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), QString());
    openAction_->setShortcut(QKeySequence::Open);
    connect(openAction_, &QAction::triggered, this, &MainWindow::chooseFile);
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
    browserToolbar_->addSeparator();
    browserToolbar_->addWidget(positionLabel_);
    browserToolbar_->addWidget(positionSpin_);
    connect(positionSpin_, &QDoubleSpinBox::editingFinished, this, [this]() {
        if (!refreshingBrowserControls_) {
            requestWindow(positionSpin_->value());
        }
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


void MainWindow::buildProcessingControls() {
    referenceLabel_ = new QLabel(this);
    referenceCombo_ = new QComboBox(this);
    referenceCombo_->addItem(QString(), static_cast<int>(dsp::ReferenceMode::Raw));
    referenceCombo_->addItem(QString(), static_cast<int>(dsp::ReferenceMode::Car));
    referenceCombo_->addItem(QString(), static_cast<int>(dsp::ReferenceMode::Bipolar));
    browserToolbar_->addSeparator();
    browserToolbar_->addWidget(referenceLabel_);
    browserToolbar_->addWidget(referenceCombo_);

    notchEnableCheck_ = new QCheckBox(this);
    browserToolbar_->addSeparator();
    browserToolbar_->addWidget(notchEnableCheck_);
    notchBandChecks_.clear();
    notchBandSpins_.clear();
    for (const NotchBandUi& band : processingState_.notchBands) {
        auto* chk = new QCheckBox(this);
        chk->setChecked(band.enabled);
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(1.0, 500.0);
        spin->setDecimals(1);
        spin->setValue(band.frequency);
        spin->setSuffix(" Hz");
        spin->setMaximumWidth(90);
        notchBandChecks_.push_back(chk);
        notchBandSpins_.push_back(spin);
        browserToolbar_->addWidget(chk);
        browserToolbar_->addWidget(spin);
    }
    notchHarmonicsCheck_ = new QCheckBox(this);
    notchHarmonicsSpin_ = new QSpinBox(this);
    notchHarmonicsSpin_->setRange(2, 20);
    notchHarmonicsSpin_->setValue(processingState_.notchHarmonicMultiple);
    notchHarmonicsSpin_->setMaximumWidth(70);
    browserToolbar_->addWidget(notchHarmonicsCheck_);
    browserToolbar_->addWidget(notchHarmonicsSpin_);

    bandpassEnableCheck_ = new QCheckBox(this);
    bandpassLowSpin_ = new QDoubleSpinBox(this);
    bandpassLowSpin_->setRange(0.1, 400.0);
    bandpassLowSpin_->setDecimals(1);
    bandpassLowSpin_->setValue(processingState_.bandpassLow);
    bandpassLowSpin_->setSuffix(" Hz");
    bandpassLowSpin_->setMaximumWidth(90);
    bandpassHighSpin_ = new QDoubleSpinBox(this);
    bandpassHighSpin_->setRange(0.5, 500.0);
    bandpassHighSpin_->setDecimals(1);
    bandpassHighSpin_->setValue(processingState_.bandpassHigh);
    bandpassHighSpin_->setSuffix(" Hz");
    bandpassHighSpin_->setMaximumWidth(90);
    browserToolbar_->addSeparator();
    browserToolbar_->addWidget(bandpassEnableCheck_);
    browserToolbar_->addWidget(bandpassLowSpin_);
    browserToolbar_->addWidget(bandpassHighSpin_);

    auto connectChanged = [this](auto* widget) {
        using W = std::remove_pointer_t<decltype(widget)>;
        if constexpr (std::is_same_v<W, QComboBox>) {
            connect(widget, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { onProcessingUiChanged(); });
        }
    };
    connect(referenceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        onProcessingUiChanged();
    });
    connect(notchEnableCheck_, &QCheckBox::toggled, this, [this](bool) { onProcessingUiChanged(); });
    for (QCheckBox* chk : notchBandChecks_) {
        connect(chk, &QCheckBox::toggled, this, [this](bool) { onProcessingUiChanged(); });
    }
    for (QDoubleSpinBox* spin : notchBandSpins_) {
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { onProcessingUiChanged(); });
    }
    connect(notchHarmonicsCheck_, &QCheckBox::toggled, this, [this](bool) { onProcessingUiChanged(); });
    connect(notchHarmonicsSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { onProcessingUiChanged(); });
    connect(bandpassEnableCheck_, &QCheckBox::toggled, this, [this](bool) { onProcessingUiChanged(); });
    connect(bandpassLowSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { onProcessingUiChanged(); });
    connect(bandpassHighSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { onProcessingUiChanged(); });
    refreshProcessingControls();
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
    retranslateUi();
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
        positionSpin_->setRange(0.0, std::max(0.0, currentFile_.session.duration - viewState_.windowSeconds));
        positionSpin_->setValue(currentFile_.waveform.t0);
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
    openButton_->setText(i18n_->text("app.action.open"));
    quitAction_->setText(i18n_->text("app.action.quit"));
    languageMenu_->setTitle(i18n_->text("app.menu.language"));
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
    if (referenceLabel_ != nullptr) {
        referenceLabel_->setText(i18n_->text("app.processing.reference"));
        referenceCombo_->setItemText(0, i18n_->text("app.processing.ref.raw"));
        referenceCombo_->setItemText(1, i18n_->text("app.processing.ref.car"));
        referenceCombo_->setItemText(2, i18n_->text("app.processing.ref.bipolar"));
        notchEnableCheck_->setText(i18n_->text("app.processing.notch"));
        notchHarmonicsCheck_->setText(i18n_->text("app.processing.harmonics"));
        bandpassEnableCheck_->setText(i18n_->text("app.processing.bandpass"));
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

    if (statusKey_ == "app.status.sidecar_failed" || statusKey_ == "app.status.file_failed") {
        statusBar()->showMessage(i18n_->text(
            statusKey_,
            {{"error", i18n_->text(QString("error.%1").arg(lastError_.code), lastError_.params)}}));
        return;
    }
    statusBar()->showMessage(i18n_->text(statusKey_, statusParams_));
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
    currentFile_ = result;
    rawWaveform_ = result.waveform;
    hasCurrentFile_ = true;
    centralStack_->setCurrentWidget(waveformWidget_);
    browserToolbar_->show();
    channelDock_->show();
    refreshBrowserControls();
    refreshProcessingControls();
    applyProcessingAndDisplay();
    refreshAnalysisControls();
    analysisDock_->show();
    requestCurrentAnalysis();
}

void MainWindow::showFileOpenError(const AppError& error) {
    setLoading(false);
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
    if (referenceCombo_ == nullptr) {
        return;
    }
    refreshingProcessingControls_ = true;
    const int refIndex = referenceCombo_->findData(static_cast<int>(processingState_.referenceMode));
    referenceCombo_->setCurrentIndex(refIndex >= 0 ? refIndex : 0);
    notchEnableCheck_->setChecked(processingState_.notchEnabled);
    for (int i = 0; i < notchBandChecks_.size() && i < static_cast<int>(processingState_.notchBands.size()); ++i) {
        notchBandChecks_[i]->setChecked(processingState_.notchBands[static_cast<std::size_t>(i)].enabled);
        notchBandSpins_[i]->setValue(processingState_.notchBands[static_cast<std::size_t>(i)].frequency);
        const bool enableBand = processingState_.notchEnabled;
        notchBandChecks_[i]->setEnabled(enableBand);
        notchBandSpins_[i]->setEnabled(enableBand);
    }
    notchHarmonicsCheck_->setChecked(processingState_.notchHarmonics);
    notchHarmonicsSpin_->setValue(processingState_.notchHarmonicMultiple);
    notchHarmonicsCheck_->setEnabled(processingState_.notchEnabled);
    notchHarmonicsSpin_->setEnabled(processingState_.notchEnabled && processingState_.notchHarmonics);
    bandpassEnableCheck_->setChecked(processingState_.globalBandpassEnabled);
    bandpassLowSpin_->setValue(processingState_.bandpassLow);
    bandpassHighSpin_->setValue(processingState_.bandpassHigh);
    bandpassLowSpin_->setEnabled(processingState_.globalBandpassEnabled);
    bandpassHighSpin_->setEnabled(processingState_.globalBandpassEnabled);
    refreshingProcessingControls_ = false;
}

void MainWindow::onProcessingUiChanged() {
    if (refreshingProcessingControls_) {
        return;
    }
    processingState_.referenceMode = static_cast<dsp::ReferenceMode>(referenceCombo_->currentData().toInt());
    processingState_.notchEnabled = notchEnableCheck_->isChecked();
    processingState_.notchBands.resize(static_cast<std::size_t>(notchBandChecks_.size()));
    for (int i = 0; i < notchBandChecks_.size(); ++i) {
        processingState_.notchBands[static_cast<std::size_t>(i)].enabled = notchBandChecks_[i]->isChecked();
        processingState_.notchBands[static_cast<std::size_t>(i)].frequency = notchBandSpins_[i]->value();
    }
    processingState_.notchHarmonics = notchHarmonicsCheck_->isChecked();
    processingState_.notchHarmonicMultiple = notchHarmonicsSpin_->value();
    processingState_.globalBandpassEnabled = bandpassEnableCheck_->isChecked();
    processingState_.bandpassLow = bandpassLowSpin_->value();
    processingState_.bandpassHigh = bandpassHighSpin_->value();
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
