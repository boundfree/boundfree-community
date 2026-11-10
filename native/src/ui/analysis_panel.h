#pragma once

#include "controller/analysis_service.h"
#include "i18n/i18n_service.h"
#include "render/waveform_slice.h"

#include <QElapsedTimer>
#include <QString>
#include <QWidget>

#include <array>
#include <functional>

class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace bf {

class BandTrendWidget;
class SpectrogramWidget;
class SpectrumWidget;

// 面板发起请求时由 MainWindow 现算的输入快照。
// signal 只在一次同步调用内有效：面板不缓存该指针，也不把它带进异步路径，
// 请求本身把所需样本复制进 AnalysisRequest。inputSummary 由 MainWindow 用既有的
// 处理链描述拼好，面板只负责显示，不重复实现处理链文案。
struct AnalysisInputContext {
    const WaveformSlice* signal = nullptr;  // 当前活动信号，含原始/处理后选择
    double windowStart = 0.0;               // WaveformWidget::timeRange().first
    double windowEnd = 0.0;                 // 同上 .second
    bool requestBlocked = false;            // 打开文件、等待窗口或拖动进度条期间不发起请求
    QString inputSummary;
};

// 分析面板：PSD/FFT、STFT、频带趋势与相干性四页的控件、请求记账和结果渲染。
// 从 MainWindow 提取；dock 的创建、固定化、加入分隔器和显隐仍归 MainWindow，
// 面板只负责 dock 内容与标题。输入摘要与通道控件是两条独立语义：语言切换和失败码
// 只更新摘要，只有信号身份/单位/采样率变化才重建下拉与 Nyquist 上限。
class AnalysisPanel final : public QWidget {
public:
    using InputProvider = std::function<AnalysisInputContext()>;

    AnalysisPanel(QDockWidget* dock, I18nService* i18n);

    // 由 MainWindow 在构造后设置一次，之后按当前浏览/设备状态现算输入。
    void setInputProvider(InputProvider provider);

    void refreshInputSummary();
    void refreshInputControls();

    // 用户操作与设备后台帧共用的请求入口；background 保留原有的
    // “同页且 pending 时合并”与“保留当前结果”语义。
    void requestRefresh(bool background = false);
    // 播放推进与预取窗口两处的 500ms 节流（后台刷新）。计时节拍属于面板：每次请求末尾重启，
    // 所以“何时调用”留在 MainWindow，“是否到期”在这里判断；未播放的前台刷新直接走
    // requestRefresh()，不经过这里。
    void requestPlaybackRefresh();
    void invalidate();

    void retranslateUi();
    void setDarkTheme(bool dark);
    void resetChannelSelections();

    // 分析页里隐藏的“发送当前选择到分离工作区”按钮：两个动作都归 MainWindow。
    std::function<void()> sendSelectionRequested;

private:
    AnalysisInputContext currentInput() const;
    QString analysisUnitLabel(AnalysisKind kind, const QString& unit, bool db) const;
    void clearAnalysisPlot(int page);
    void refreshAnalysisPage(int page);
    void renderAnalysisResult(int page);
    void handleAnalysisCompleted(int generation, const AnalysisResult& result);
    void handleAnalysisFailed(int generation, const QString& code, const QString& detail);

    QDockWidget* dock_ = nullptr;
    I18nService* i18n_ = nullptr;
    InputProvider inputProvider_;

    QTabWidget* analysisTabs_ = nullptr;
    QLabel* analysisHeading_ = nullptr;
    QLabel* analysisInputSummary_ = nullptr;
    QPushButton* selectionButton_ = nullptr;
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
    bool refreshingAnalysisControls_ = false;
    QElapsedTimer analysisElapsed_;
};

}  // namespace bf
