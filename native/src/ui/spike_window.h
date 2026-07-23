#pragma once

#include "core/version.h"
#include "dsp/spike_types.h"

#include <QMainWindow>
#include <QString>

#include <memory>

class QTabWidget;
class QLabel;

namespace bf {

class I18nService;
class SpikeAnalysisService;
class SpikeClusterWidget;
class SpikeRasterWidget;
class SpikePsthWidget;
class SpikeIsiWidget;

// Spike 分析线主窗口。组合 SpikeAnalysisService 与 4 个 Spike 视图，
// 通过 SidecarClient 打开文件、取片、组装 SpikeAnalysisRequest 并触发分析。
// fs 严格来自打开文件的 metadata（SidecarOpenResult.fs / SidecarSliceResult.fs），
// 禁止硬编码采样率。
class SpikeWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit SpikeWindow(const VersionInfo& version,
                         I18nService* i18n,
                         const QString& repoRoot = {},
                         QWidget* parent = nullptr);
    ~SpikeWindow() override;

    // 供冒烟直接调用（免 QFileDialog）。打开 path → startAndHello → open →
    // getSlice → request(spike analysis)。同步返回是否已成功发起分析请求。
    bool openPath(const QString& path);

    // 冒烟查询：分析是否已至少完成一次（成功或失败）。
    bool analysisCompleted() const { return analysisCompleted_; }
    bool analysisSucceeded() const { return analysisSucceeded_; }
    int lastTotalSpikes() const { return lastTotalSpikes_; }
    int lastClusterCount() const { return lastClusterCount_; }

signals:
    // 冒烟用：分析完成（成功或失败）后发出，便于事件循环退出。
    void analysisSettled(bool ok);

private slots:
    void onOpenFile();
    void onAnalysisCompleted(int generation, const dsp::SpikeAnalysis& result);
    void onAnalysisFailed(int generation, const QString& code, const QString& detail);

private:
    void buildMenu();
    void setStatusText(const QString& text);

    VersionInfo version_;
    I18nService* i18n_ = nullptr;
    QString repoRoot_;

    SpikeAnalysisService* service_ = nullptr;
    int generation_ = -1;

    SpikeClusterWidget* clusterWidget_ = nullptr;
    SpikeRasterWidget* rasterWidget_ = nullptr;
    SpikePsthWidget* psthWidget_ = nullptr;
    SpikeIsiWidget* isiWidget_ = nullptr;

    QTabWidget* tabs_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    bool analysisCompleted_ = false;
    bool analysisSucceeded_ = false;
    int lastTotalSpikes_ = 0;
    int lastClusterCount_ = 0;
};

}  // namespace bf
