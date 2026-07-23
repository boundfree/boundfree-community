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

// Spike-line main window. Combines SpikeAnalysisService with the four Spike views,
// opens a file via SidecarClient, fetches a slice, builds a SpikeAnalysisRequest and triggers analysis.
// fs comes strictly from the opened file metadata (SidecarOpenResult.fs / SidecarSliceResult.fs);
// sampling rate is never hardcoded.
class SpikeWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit SpikeWindow(const VersionInfo& version,
                         I18nService* i18n,
                         const QString& repoRoot = {},
                         QWidget* parent = nullptr);
    ~SpikeWindow() override;

    // For direct smoke use (no QFileDialog). Open path -> startAndHello -> open ->
    // getSlice -> request(spike analysis). Returns synchronously whether the request was issued.
    bool openPath(const QString& path);

    // Smoke query: whether analysis has settled at least once (success or failure).
    bool analysisCompleted() const { return analysisCompleted_; }
    bool analysisSucceeded() const { return analysisSucceeded_; }
    int lastTotalSpikes() const { return lastTotalSpikes_; }
    int lastClusterCount() const { return lastClusterCount_; }

signals:
    // Smoke: emitted after analysis settles (success or failure) so the event loop can exit.
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
