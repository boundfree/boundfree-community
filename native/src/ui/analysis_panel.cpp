#include "ui/analysis_panel.h"

#include "ui/analysis_band_table.h"
#include "ui/analysis_properties.h"
#include "ui/analysis_widgets.h"
#include "ui/control_bar.h"
#include "ui/fluent_controls.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace bf {

AnalysisPanel::AnalysisPanel(QDockWidget* dock, I18nService* i18n) : QWidget(dock), dock_(dock), i18n_(i18n) {
    analysisTabs_ = new QTabWidget(dock);
    analysisTabs_->setObjectName("eegAnalysisTabs");
    auto* analysisLayout = new QVBoxLayout(this);
    analysisLayout->setContentsMargins(4, 4, 4, 4);
    analysisLayout->setSpacing(8);
    analysisHeading_ = new QLabel(this);
    analysisHeading_->setObjectName("eegAnalysisHeading");
    auto headingFont = analysisHeading_->font(); headingFont.setBold(true);
    analysisHeading_->setFont(headingFont);
    analysisLayout->addWidget(analysisHeading_);
    analysisLayout->addWidget(analysisTabs_, 1);
    analysisInputSummary_ = new QLabel(this);
    analysisInputSummary_->setObjectName("eegAnalysisInputSummary");
    analysisInputSummary_->setWordWrap(true);
    analysisInputSummary_->setTextFormat(Qt::PlainText);
    analysisInputSummary_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    analysisLayout->addWidget(analysisInputSummary_);
    selectionButton_=new ActionButton(this);selectionButton_->setObjectName("signalSendToSeparation");
    selectionButton_->hide(); // Workspace navigation already opens the same current-selection snapshot.
    connect(selectionButton_,&QPushButton::clicked,this,[this]{if(sendSelectionRequested)sendSelectionRequested();});
    auto* analysisScroll=new QScrollArea(dock);analysisScroll->setObjectName("signalAnalysisScroll");
    analysisScroll->setWidgetResizable(true);analysisScroll->setFrameShape(QFrame::NoFrame);
    analysisScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);analysisScroll->setMinimumSize(180,100);
    analysisScroll->setWidget(this);dock_->setWidget(analysisScroll);
    dock_->hide();

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
        // Reserve the full wrapped text height outside the graph. Analysis
        // metadata is part of the visible result, not a nested scroll region.
        auto* content = new AnalysisProperties(page);
        content->setObjectName(QString("eegAnalysisProperties_%1").arg(index));
        auto* properties = new QVBoxLayout(content);
        properties->setContentsMargins(2, 0, 2, 0);
        properties->setSpacing(2);
        properties->addWidget(analysisStatusLabels_[index]);
        auto* legend = new QLabel(content);
        legend->setObjectName(QString("eegAnalysisLegend_%1").arg(index));
        legend->setWordWrap(true);
        legend->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
        properties->addWidget(legend);
        analysisLegendLabels_[index] = legend;
        if (summary == nullptr) summary = new AnalysisSummaryLabel(content);
        summary->setObjectName(QString("eegAnalysisSummary_%1").arg(index));
        // The window style sheet stops setFont() propagation; keep result tables at workbench size.
        summary->setFont(workbenchFont());
        summary->setTextFormat(Qt::RichText);
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        summary->setWordWrap(true);
        summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
        properties->addWidget(summary);
        analysisSummaryLabels_[index] = summary;
        layout->addWidget(content);
    };
    auto* psdPage = new QWidget(analysisTabs_);
    auto* psdLayout = new QVBoxLayout(psdPage);
    addHeader(psdPage, psdLayout, 0);
    auto* psdControls = new ControlFlowLayout(nullptr);
    psdChannelCombo_ = new ChoiceBox(psdPage);
    psdModeCombo_ = new ChoiceBox(psdPage);
    psdDbCheck_ = new CheckOption(psdPage);
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
    spectrumWidget_->setMinimumSize(180, 160);
    spectrumWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    psdLayout->addLayout(psdControls);
    psdLayout->addWidget(spectrumWidget_, 1);
    psdBandSummaryLabel_ = new AnalysisSummaryLabel(psdPage);
    psdBandSummaryLabel_->setWordWrap(true);
    psdBandSummaryLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    addFooter(psdPage, psdLayout, 0, psdBandSummaryLabel_);
    analysisTabs_->addTab(psdPage, QString());

    auto* stftPage = new QWidget(analysisTabs_);
    auto* stftLayout = new QVBoxLayout(stftPage);
    addHeader(stftPage, stftLayout, 1);
    auto* stftControls = new ControlFlowLayout(nullptr);
    stftChannelCombo_ = new ChoiceBox(stftPage);
    stftResolutionCombo_ = new ChoiceBox(stftPage);
    stftFmaxSpin_ = new QSpinBox(stftPage);
    stftFmaxSpin_->setRange(5, 500);
    stftFmaxSpin_->setValue(100);
    stftFmaxSpin_->setSuffix(" Hz");
    stftControls->addWidget(stftChannelCombo_);
    stftControls->addWidget(stftResolutionCombo_);
    stftControls->addWidget(stftFmaxSpin_);
    spectrogramWidget_ = new SpectrogramWidget(stftPage);
    spectrogramWidget_->setObjectName("eegAnalysisPlot_1");
    spectrogramWidget_->setMinimumSize(180, 160);
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
    trendChannelCombo_ = new ChoiceBox(trendPage);
    trendEpochSpin_ = new QDoubleSpinBox(trendPage);
    trendEpochSpin_->setRange(0.5, 30.0);
    trendEpochSpin_->setValue(1.0);
    trendEpochSpin_->setSuffix(" s");
    trendControls->addWidget(trendChannelCombo_);
    trendControls->addWidget(trendEpochSpin_);
    trendWidget_ = new BandTrendWidget(trendPage);
    trendWidget_->setObjectName("eegAnalysisPlot_2");
    trendWidget_->setMinimumSize(180, 160);
    trendWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    trendLayout->addLayout(trendControls);
    trendLayout->addWidget(trendWidget_, 1);
    addFooter(trendPage, trendLayout, 2);
    analysisTabs_->addTab(trendPage, QString());

    auto* coherencePage = new QWidget(analysisTabs_);
    auto* coherenceLayout = new QVBoxLayout(coherencePage);
    addHeader(coherencePage, coherenceLayout, 3);
    auto* coherenceControls = new ControlFlowLayout(nullptr);
    coherenceACombo_ = new ChoiceBox(coherencePage);
    coherenceBCombo_ = new ChoiceBox(coherencePage);
    coherenceFmaxSpin_ = new QSpinBox(coherencePage);
    coherenceFmaxSpin_->setRange(5, 500);
    coherenceFmaxSpin_->setValue(100);
    coherenceFmaxSpin_->setSuffix(" Hz");
    const auto labeledControl = [coherencePage, coherenceControls](QLabel*& label, QWidget* control) {
        auto* pair = new QWidget(coherencePage);
        auto* row = new QVBoxLayout(pair);
        row->setContentsMargins(0, 0, 0, 0); row->setSpacing(4);
        label = new QLabel(pair); label->setBuddy(control);
        row->addWidget(label); row->addWidget(control);
        coherenceControls->addWidget(pair);
    };
    coherenceACombo_->setObjectName("eegCoherenceChannelA");
    coherenceBCombo_->setObjectName("eegCoherenceChannelB");
    coherenceFmaxSpin_->setObjectName("eegCoherenceFmax");
    labeledControl(coherenceALabel_, coherenceACombo_);
    labeledControl(coherenceBLabel_, coherenceBCombo_);
    labeledControl(coherenceFmaxLabel_, coherenceFmaxSpin_);
    coherenceWidget_ = new SpectrumWidget(coherencePage);
    coherenceWidget_->setObjectName("eegAnalysisPlot_3");
    coherenceWidget_->setMinimumSize(180, 160);
    coherenceWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    coherenceSummaryLabel_ = new AnalysisSummaryLabel(coherencePage);
    coherenceSummaryLabel_->setWordWrap(true);
    coherenceSummaryLabel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    coherenceLayout->addLayout(coherenceControls);
    coherenceLayout->addWidget(coherenceWidget_, 1);
    addFooter(coherencePage, coherenceLayout, 3, coherenceSummaryLabel_);
    analysisTabs_->addTab(coherencePage, QString());

    for (auto* combo : {psdChannelCombo_, psdModeCombo_, stftChannelCombo_, stftResolutionCombo_,
                        trendChannelCombo_, coherenceACombo_, coherenceBCombo_}) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setMinimumContentsLength(6);
    }

    analysisService_ = new AnalysisService(this);
    connect(analysisService_, &AnalysisService::completed, this, &AnalysisPanel::handleAnalysisCompleted);
    connect(analysisService_, &AnalysisService::failed, this, &AnalysisPanel::handleAnalysisFailed);
    connect(analysisTabs_, &QTabWidget::currentChanged, this, [this](int) { requestRefresh(); });
    const auto reanalyze = [this]() { if (!refreshingAnalysisControls_) requestRefresh(); };
    for (QComboBox* combo : {psdChannelCombo_, psdModeCombo_, stftChannelCombo_, stftResolutionCombo_, trendChannelCombo_, coherenceACombo_, coherenceBCombo_}) {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, reanalyze);
    }
    for (QSpinBox* spin : {psdFmaxSpin_, stftFmaxSpin_, coherenceFmaxSpin_}) {
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, reanalyze);
    }
    connect(psdDbCheck_, &QCheckBox::toggled, this, reanalyze);
    connect(trendEpochSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, reanalyze);
}

void AnalysisPanel::setInputProvider(InputProvider provider) {
    inputProvider_ = std::move(provider);
}

AnalysisInputContext AnalysisPanel::currentInput() const {
    return inputProvider_ ? inputProvider_() : AnalysisInputContext{};
}

void AnalysisPanel::refreshInputSummary() {
    if (analysisInputSummary_ == nullptr) return;
    analysisInputSummary_->setText(currentInput().inputSummary);
}

void AnalysisPanel::refreshInputControls() {
    if (analysisTabs_ == nullptr) {
        return;
    }
    refreshingAnalysisControls_ = true;
    const AnalysisInputContext input = currentInput();
    const QStringList names = input.signal != nullptr ? input.signal->channelNames : QStringList{};
    for (QComboBox* combo : {psdChannelCombo_, stftChannelCombo_, trendChannelCombo_, coherenceACombo_, coherenceBCombo_}) {
        const int selected = combo->currentIndex();
        combo->clear();
        combo->addItems(names);
        combo->setCurrentIndex(std::clamp(selected, 0, std::max(0, combo->count() - 1)));
    }
    if (coherenceBCombo_->count() > 1 && coherenceBCombo_->currentIndex() == coherenceACombo_->currentIndex()) {
        coherenceBCombo_->setCurrentIndex(1);
    }
    const int nyquist = input.signal != nullptr ? std::max(1, static_cast<int>(input.signal->fs / 2.0)) : 500;
    for (QSpinBox* spin : {psdFmaxSpin_, stftFmaxSpin_, coherenceFmaxSpin_}) {
        spin->setMinimum(std::min(5,nyquist));
        spin->setMaximum(nyquist);
        spin->setValue(std::min(spin->value(), nyquist));
    }
    refreshingAnalysisControls_ = false;
}

void AnalysisPanel::requestRefresh(bool background) {
    if (analysisService_ == nullptr || refreshingAnalysisControls_) {
        return;
    }
    // provider 每次请求现算输入；signal 只在本次调用内使用，样本由 AnalysisRequest 复制。
    const AnalysisInputContext input = currentInput();
    if (input.signal == nullptr || input.requestBlocked) return;
    const int currentTab = analysisTabs_->currentIndex();
    if (currentTab < 0 || currentTab >= 4) return;
    // Automatic playback updates must let an expensive analysis finish. User
    // changes still supersede work immediately via a new generation.
    if (background && requestedAnalysisPage_ == currentTab && analysisRequestPending_) return;
    const WaveformSlice& signal = *input.signal;
    AnalysisRequest request;
    request.fs = signal.fs;
    if (!(request.fs > 0.0)) return;
    const double start = input.windowStart;
    const double end = input.windowEnd;
    const auto window=signalWindowRange(signal,start,end);
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
    std::vector<int> selected{request.channel};
    if(request.kind==AnalysisKind::Coherence)selected.push_back(request.channelB);
    const auto spans=signalContinuousRanges(window,selected);
    if(spans.empty()) {
        auto& page=analysisPages_[currentTab];page={};page.state=AnalysisDisplayState::Empty;
        clearAnalysisPlot(currentTab);refreshAnalysisPage(currentTab);return;
    }
    const auto span=*std::max_element(spans.begin(),spans.end(),[](const auto& a,const auto& b){return a.last-a.first<b.last-b.first;});
    if(span.last-span.first<2)return;
    for(const auto& channel:window.data) {
        if(span.last>channel.size())return;
        request.data.emplace_back(channel.begin()+span.first,channel.begin()+span.last);
    }
    request.tStart=span.start;
    request.unit=signalUnit(window,request.channel);request.sourceId=window.sourceId;request.streamId=window.streamId;request.sourceContext=window.metadata;
    if (requestedAnalysisPage_ >= 0 && requestedAnalysisPage_ != currentTab)
        analysisStatusLabels_[requestedAnalysisPage_]->setProperty("analysisUpdating", false);
    // Every request owns one page and its exact input context. A pending result
    // from a previously selected page must never repopulate a different page.
    if (requestedAnalysisPage_ >= 0 && requestedAnalysisPage_ != currentTab
        && analysisPages_[requestedAnalysisPage_].state == AnalysisDisplayState::Pending) {
        analysisPages_[requestedAnalysisPage_] = {};
        analysisStatusLabels_[requestedAnalysisPage_]->setProperty("analysisGeneration", 0);
        clearAnalysisPlot(requestedAnalysisPage_);
        refreshAnalysisPage(requestedAnalysisPage_);
    }
    auto& displayed = analysisPages_[currentTab];
    const bool retainResult = background && displayed.state == AnalysisDisplayState::Ready;
    AnalysisPageState page;
    page.state = AnalysisDisplayState::Pending;
    page.kind = request.kind;
    page.start = request.tStart;
    page.end = request.tStart + (request.data.empty() ? 0.0 : request.data.front().size() / request.fs);
    page.fmax = request.fmax;
    page.asDb = request.kind == AnalysisKind::Psd && psdDbCheck_->isChecked();
    page.unit=request.unit;page.partialWindow=spans.size()>1;
    page.channel = signal.channelNames.value(request.channel);
    if (request.kind == AnalysisKind::Coherence) {
        page.channel += QStringLiteral(" / ") + signal.channelNames.value(request.channelB);
    }
    requestedAnalysisPage_ = currentTab;
    pendingAnalysis_ = std::move(page);
    analysisRequestPending_ = true;
    // Background playback work keeps the complete last result on screen.
    // Its time interval belongs to that displayed result until completion.
    if (!retainResult) {
        displayed = pendingAnalysis_;
        clearAnalysisPlot(currentTab);
        refreshAnalysisPage(currentTab);
    }
    analysisStatusLabels_[currentTab]->setProperty("analysisUpdating", retainResult);
    analysisGeneration_ = analysisService_->request(request);
    analysisStatusLabels_[currentTab]->setProperty("analysisGeneration", analysisGeneration_);
    analysisElapsed_.restart();
}

void AnalysisPanel::requestPlaybackRefresh(bool background) {
    if (!analysisElapsed_.isValid() || analysisElapsed_.elapsed() >= 500) requestRefresh(background);
}

void AnalysisPanel::invalidate() {
    if (analysisService_ != nullptr) analysisService_->cancel();
    analysisGeneration_ = 0;
    requestedAnalysisPage_ = -1;
    analysisRequestPending_ = false;
    pendingAnalysis_ = {};
    for (int index = 0; index < 4; ++index) {
        analysisPages_[index] = {};
        if (analysisStatusLabels_[index] != nullptr) {
            analysisStatusLabels_[index]->setProperty("analysisGeneration", 0);
            analysisStatusLabels_[index]->setProperty("analysisUpdating", false);
        }
        clearAnalysisPlot(index);
        refreshAnalysisPage(index);
    }
}

void AnalysisPanel::clearAnalysisPlot(int page) {
    if (page == 0) spectrumWidget_->setSpectrum({}, {}, {});
    else if (page == 1) spectrogramWidget_->setSpectrogram({});
    else if (page == 2) trendWidget_->setTrend({}, {});
    else coherenceWidget_->setSpectrum({}, {}, {}, true);
    if (analysisSummaryLabels_[page] != nullptr) analysisSummaryLabels_[page]->clear();
}

void AnalysisPanel::refreshAnalysisPage(int index) {
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
    analysisStatusLabels_[index]->setText(stateText + (context.isEmpty() ? QString() : QStringLiteral("\n") + context) + (page.partialWindow?QStringLiteral("\n")+i18n_->text("app.signal.continuous_segment"):QString()));
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
    const bool hasBandTable = page.state == AnalysisDisplayState::Ready
        && (page.kind == AnalysisKind::Psd || page.kind == AnalysisKind::Coherence);
    analysisLegendLabels_[index]->setVisible(!bands.isEmpty() && !hasBandTable);
    QString yLabel;
    if (index == 0) yLabel = analysisUnitLabel(page.kind,page.unit,page.asDb);
    else if (index == 1) yLabel = i18n_->text("app.analysis.frequency");
    else if (index == 2) yLabel = analysisUnitLabel(AnalysisKind::BandTrend,page.unit,false);
    else yLabel = i18n_->text("app.analysis.coherence");
    const QString empty = QString();
    if (index == 0) spectrumWidget_->setAxisLabels(i18n_->text("app.analysis.frequency"), yLabel, empty);
    else if (index == 1) spectrogramWidget_->setLabels(i18n_->text(titleKey), i18n_->text("app.analysis.time"), yLabel, empty);
    else if (index == 2) trendWidget_->setAxisLabels(i18n_->text("app.analysis.time"), yLabel, empty);
    else coherenceWidget_->setAxisLabels(i18n_->text("app.analysis.frequency"), yLabel, empty);
    const std::array<QWidget*, 4> plots{spectrumWidget_, spectrogramWidget_, trendWidget_, coherenceWidget_};
    plots[index]->setLocale(i18n_->qlocale());
}

void AnalysisPanel::handleAnalysisCompleted(int generation, const AnalysisResult& result) {
    if (generation != analysisGeneration_ || requestedAnalysisPage_ < 0) return;
    if (!analysisRequestPending_ || result.kind != pendingAnalysis_.kind) return;
    auto& page = analysisPages_[requestedAnalysisPage_];
    const auto finiteVector = [](const std::vector<double>& values) {
        return !values.empty() && std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
    };
    bool valid = false;
    switch (result.kind) {
    case AnalysisKind::Psd:
        // 频段功率由分析任务按 PSD 算好；数量不符说明生产者与展示契约不一致，
        // 不能退化为界面自行计算，否则重新回到“界面实现算法”。
        valid = finiteVector(result.spectrum.frequencies) && finiteVector(result.spectrum.values)
            && result.spectrum.frequencies.size() == result.spectrum.values.size()
            && result.bandPowers.size() == dsp::eegBands().size() && finiteVector(result.bandPowers);
        break;
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
    QWidget* surface = analysisTabs_->widget(requestedAnalysisPage_);
    const bool updatesEnabled = surface->updatesEnabled();
    if (updatesEnabled) surface->setUpdatesEnabled(false);
    const auto restoreUpdates = qScopeGuard([surface, updatesEnabled]() { if (updatesEnabled) surface->setUpdatesEnabled(true); });
    page = pendingAnalysis_;
    analysisRequestPending_ = false;
    analysisStatusLabels_[requestedAnalysisPage_]->setProperty("analysisUpdating", false);
    page.state = valid ? AnalysisDisplayState::Ready : AnalysisDisplayState::Empty;
    if (valid) { page.result = result; renderAnalysisResult(requestedAnalysisPage_); }
    else { page.result = {}; clearAnalysisPlot(requestedAnalysisPage_); }
    refreshAnalysisPage(requestedAnalysisPage_);
}

void AnalysisPanel::renderAnalysisResult(int index) {
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
            analysisUnitLabel(result.kind,page.unit,asDb));
        if (!fft) {
            psdBandSummaryLabel_->setText(analysisBandTable(i18n_, analysisUnitLabel(AnalysisKind::BandTrend,page.unit,false), result.bandPowers));
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
        analysisSummaryLabels_[1]->setText(i18n_->text(page.unit=="uV"?"app.analysis.color_range":"app.signal.color_range", {
            {"unit",page.unit},
            {"min", i18n_->formatNumber(low, 1)}, {"max", i18n_->formatNumber(high, 1)},
        }));
        break;
    }
    case AnalysisKind::BandTrend:
        trendWidget_->setTrend(result.trend.times, result.trend.powers);
        analysisSummaryLabels_[2]->setText(analysisUnitLabel(AnalysisKind::BandTrend,page.unit,false));
        break;

    case AnalysisKind::Coherence: {
        coherenceWidget_->setSpectrum(result.spectrum, i18n_->text("app.analysis.coherence"), i18n_->text("app.analysis.coherence"), true);
        std::vector<double> values;
        for (const dsp::Band& band : dsp::eegBands()) {
            double sum = 0.0;
            int count = 0;
            for (std::size_t i = 0; i < result.spectrum.frequencies.size(); ++i) {
                if (result.spectrum.frequencies[i] >= band.low && result.spectrum.frequencies[i] <= band.high) {
                    sum += result.spectrum.values[i];
                    ++count;
                }
            }
            values.push_back(count > 0 ? sum / count : 0.0);
        }
        coherenceSummaryLabel_->setText(analysisBandTable(i18n_, i18n_->text("app.analysis.coherence"), values));
        break;
    }
    }
}

QString AnalysisPanel::analysisUnitLabel(AnalysisKind kind,const QString& unit,bool db) const {
    const bool voltage=unit=="uV" || unit==QString::fromUtf8("µV") || unit==QString::fromUtf8("μV");
    if(kind==AnalysisKind::Coherence)return i18n_->text("app.analysis.coherence");
    if(voltage)return i18n_->text(kind==AnalysisKind::Fft?"app.analysis.amplitude":kind==AnalysisKind::BandTrend?"app.analysis.trend_unit":db?"app.analysis.db_unit":"app.analysis.psd_unit");
    return i18n_->text(kind==AnalysisKind::Fft?"app.signal.amplitude_unit":kind==AnalysisKind::BandTrend?"app.signal.power_unit":db?"app.signal.db_unit":"app.signal.psd_unit",{{"unit",unit.isEmpty() || unit=="unknown"?i18n_->text("app.device.unknown_unit"):unit}});
}

void AnalysisPanel::handleAnalysisFailed(int generation, const QString& code, const QString&) {
    if (generation != analysisGeneration_ || requestedAnalysisPage_ < 0) return;
    auto& page = analysisPages_[requestedAnalysisPage_];
    if (analysisRequestPending_) page = pendingAnalysis_;
    analysisRequestPending_ = false;
    analysisStatusLabels_[requestedAnalysisPage_]->setProperty("analysisUpdating", false);
    page.state = AnalysisDisplayState::Failed;
    page.errorCode = code;
    page.result = {};
    clearAnalysisPlot(requestedAnalysisPage_);
    refreshAnalysisPage(requestedAnalysisPage_);
}

void AnalysisPanel::retranslateUi() {
    // 重译控件文本不能触发新的分析请求：与 MainWindow 原有的守卫一致。
    const QScopedValueRollback<bool> analysisTranslationGuard(refreshingAnalysisControls_, true);
    analysisHeading_->setText(i18n_->text("app.analysis.panel"));
    selectionButton_->setText(i18n_->text("app.signal.separate_selection"));
    coherenceALabel_->setText(i18n_->text("app.analysis.channel_a"));
    coherenceBLabel_->setText(i18n_->text("app.analysis.channel_b"));
    coherenceFmaxLabel_->setText(i18n_->text("app.analysis.frequency_limit"));
    coherenceACombo_->setAccessibleName(coherenceALabel_->text());
    coherenceBCombo_->setAccessibleName(coherenceBLabel_->text());
    coherenceFmaxSpin_->setAccessibleName(coherenceFmaxLabel_->text());
    refreshInputSummary();
    dock_->setWindowTitle(i18n_->text("app.analysis.panel"));
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
}

void AnalysisPanel::setDarkTheme(bool dark) {
    if (spectrumWidget_ != nullptr) spectrumWidget_->setDarkTheme(dark);
    if (spectrogramWidget_ != nullptr) spectrogramWidget_->setDarkTheme(dark);
    if (trendWidget_ != nullptr) trendWidget_->setDarkTheme(dark);
    if (coherenceWidget_ != nullptr) coherenceWidget_->setDarkTheme(dark);
}

void AnalysisPanel::resetChannelSelections() {
    for (auto* combo : {psdChannelCombo_, stftChannelCombo_, trendChannelCombo_, coherenceACombo_, coherenceBCombo_}) {
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(0);
    }
    const QSignalBlocker blocker(coherenceBCombo_);
    if (coherenceBCombo_->count() > 1) coherenceBCombo_->setCurrentIndex(1);
}

}  // namespace bf
