#include "ui/spike_window.h"

#include "controller/spike_analysis_service.h"
#include "core/app_error.h"
#include "core/version.h"
#include "dsp/spike_detect.h"
#include "dsp/spike_sort.h"
#include "dsp/spike_types.h"
#include "i18n/i18n_service.h"
#include "ipc/sidecar_client.h"
#include "ui/spike_widgets.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>

#include <QEventLoop>
#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace bf {

namespace {
// Take a slice of the first few seconds (avoids pulling the whole record at high sampling rates).
// If total duration is shorter, take the whole record.
constexpr double kSliceSeconds = 2.0;

// Assemble a single-channel double signal from the given channel of SidecarSliceResult.
// data is float [nChannels][nSamples].
std::vector<double> extractChannel(const SidecarSliceResult& slice, int channel) {
    std::vector<double> signal;
    if (slice.data.empty()) {
        return signal;
    }
    const int nChannels = static_cast<int>(slice.data.size());
    const int ch = std::clamp(channel, 0, nChannels - 1);
    const auto& chan = slice.data[static_cast<std::size_t>(ch)];
    signal.reserve(chan.size());
    for (float v : chan) {
        signal.push_back(static_cast<double>(v));
    }
    return signal;
}
}  // namespace

SpikeWindow::SpikeWindow(const VersionInfo& version,
                         I18nService* i18n,
                         const QString& repoRoot,
                         QWidget* parent)
    : QMainWindow(parent), version_(version), i18n_(i18n), repoRoot_(repoRoot) {
    setWindowTitle(QStringLiteral("BoundFree Spike"));

    service_ = new SpikeAnalysisService(this);
    connect(service_, &SpikeAnalysisService::completed,
            this, &SpikeWindow::onAnalysisCompleted);
    connect(service_, &SpikeAnalysisService::failed,
            this, &SpikeWindow::onAnalysisFailed);

    clusterWidget_ = new SpikeClusterWidget(this);
    rasterWidget_ = new SpikeRasterWidget(this);
    psthWidget_ = new SpikePsthWidget(this);
    isiWidget_ = new SpikeIsiWidget(this);

    tabs_ = new QTabWidget(this);
    tabs_->addTab(clusterWidget_, QStringLiteral("Cluster"));
    tabs_->addTab(rasterWidget_, QStringLiteral("Raster"));
    tabs_->addTab(psthWidget_, QStringLiteral("PSTH"));
    tabs_->addTab(isiWidget_, QStringLiteral("ISI"));
    setCentralWidget(tabs_);

    statusLabel_ = new QLabel(QStringLiteral("No file"), this);
    statusBar()->addPermanentWidget(statusLabel_);

    buildMenu();
    resize(900, 600);
}

SpikeWindow::~SpikeWindow() = default;

void SpikeWindow::buildMenu() {
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    QAction* openAction = new QAction(QStringLiteral("&Open..."), this);
    openAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_O));
    connect(openAction, &QAction::triggered, this, &SpikeWindow::onOpenFile);
    fileMenu->addAction(openAction);

    QAction* exportAction = new QAction(i18n_->text(QStringLiteral("app.spike.export.action")), this);
    connect(exportAction, &QAction::triggered, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, i18n_->text(QStringLiteral("app.spike.export.action")),
            QStringLiteral("spike_events.csv"),
            QStringLiteral("CSV (*.csv);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }
        exportSpikeResult(path);
    });
    fileMenu->addAction(exportAction);
    fileMenu->addSeparator();
    QAction* quitAction = new QAction(QStringLiteral("&Quit"), this);
    quitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);
    fileMenu->addAction(quitAction);
}

void SpikeWindow::onOpenFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open recording"),
        QString(),
        QStringLiteral("Recordings (*.rhd *.rhs *.h5 *.csv *.dat *.edf);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    openPath(path);
}

void SpikeWindow::setStatusText(const QString& text) {
    statusLabel_->setText(text);
}

bool SpikeWindow::openPath(const QString& path) {
    const QFileInfo info(path);
    setStatusText(QStringLiteral("Opening %1...").arg(info.fileName()));

    // Sidecar: startAndHello -> openFile -> getSlice.
    // fs comes strictly from metadata, never hardcoded.
    SidecarClient sidecar(repoRoot_);
    const SidecarHelloResult hello = sidecar.startAndHello(version_);
    if (!hello.ok) {
        setStatusText(QStringLiteral("hello failed: %1").arg(hello.error.code));
        return false;
    }

    const SidecarOpenResult opened = sidecar.openFile(path);
    if (!opened.ok) {
        sidecar.stop();
        setStatusText(QStringLiteral("open failed: %1").arg(opened.error.code));
        return false;
    }

    const double duration = opened.duration > 0.0 ? opened.duration : 0.0;
    const double sliceEnd = std::min(duration, kSliceSeconds);
    const double sliceStart = 0.0;
    const double safeEnd = sliceEnd > sliceStart ? sliceEnd : sliceStart;

    const SidecarSliceResult slice = sidecar.getSlice(opened.handle, sliceStart, safeEnd);
    sidecar.stop();

    if (!slice.ok || slice.data.empty() || slice.data.front().empty()) {
        setStatusText(QStringLiteral("slice failed: %1").arg(slice.error.code));
        return false;
    }

    // fs flows in from metadata: prefer open's fs, fall back to slice's fs (they agree).
    const double fs = opened.fs > 0.0 ? opened.fs : slice.fs;
    if (fs <= 0.0) {
        setStatusText(QStringLiteral("invalid fs from metadata"));
        return false;
    }

    const int channel = 0;
    const std::vector<double> signal = extractChannel(slice, channel);
    if (signal.empty()) {
        setStatusText(QStringLiteral("empty signal"));
        return false;
    }

    // Detection config: fs must come from metadata.
    dsp::SpikeDetectConfig detect;
    detect.fs = fs;  // <- flows from metadata, not hardcoded
    detect.thresholdMad = 4.0;
    detect.detectNegative = true;
    detect.refractoryMs = 1.0;
    detect.preMs = 0.5;
    detect.postMs = 1.0;

    dsp::SpikeSortConfig sort;
    sort.nComponents = 3;
    sort.nClusters = 3;
    sort.maxIters = 100;
    sort.seed = 42;

    SpikeAnalysisRequest req;
    req.signal = signal;
    req.fs = fs;  // <- flows from metadata
    req.duration = std::min(duration, static_cast<double>(signal.size()) / fs);
    req.detect = detect;
    req.sort = sort;
    req.channel = channel;

    analysisCompleted_ = false;
    analysisSucceeded_ = false;
    lastTotalSpikes_ = 0;
    lastClusterCount_ = 0;

    generation_ = service_->request(req);
    setStatusText(QStringLiteral("Analyzing %1 (fs=%2 Hz, %3 samples)...")
                      .arg(info.fileName())
                      .arg(fs)
                      .arg(signal.size()));
    return true;
}

void SpikeWindow::onAnalysisCompleted(int generation, const dsp::SpikeAnalysis& result) {
    if (!service_->isCurrent(generation)) {
        return;
    }
    analysisCompleted_ = true;
    analysisSucceeded_ = true;
    lastTotalSpikes_ = result.totalSpikes;
    lastClusterCount_ = static_cast<int>(result.clusters.size());
    lastResult_ = result;  // cache for exportSpikeResult
    clusterWidget_->setAnalysis(result);
    rasterWidget_->setAnalysis(result);
    psthWidget_->setAnalysis(result);
    isiWidget_->setAnalysis(result);

    setStatusText(QStringLiteral("spikes=%1 clusters=%2 fs=%3 Hz")
                      .arg(result.totalSpikes)
                      .arg(static_cast<int>(result.clusters.size()))
                      .arg(result.fs));
    emit analysisSettled(true);
}

void SpikeWindow::onAnalysisFailed(int generation, const QString& code, const QString& detail) {
    if (!service_->isCurrent(generation)) {
        return;
    }
    analysisCompleted_ = true;
    analysisSucceeded_ = false;
    setStatusText(QStringLiteral("analysis failed: %1").arg(code));
    Q_UNUSED(detail)
    emit analysisSettled(false);
}

bool SpikeWindow::exportSpikeResult(const QString& outputPath) {
    // No analysis result yet: report via i18n and bail.
    if (!analysisSucceeded_ || lastResult_.clusters.empty()) {
        setStatusText(i18n_->text(QStringLiteral("app.spike.export.no_result")));
        return false;
    }

    // Collect (cluster_id, time_s) pairs across all clusters, then sort by time
    // ascending so the events CSV is ordered like the marker CSV sidecar.
    struct SpikeEvent {
        int clusterId;
        double time;
    };
    std::vector<SpikeEvent> events;
    for (const auto& cluster : lastResult_.clusters) {
        for (double t : cluster.timestamps) {
            events.push_back({cluster.id, t});
        }
    }
    std::sort(events.begin(), events.end(),
              [](const SpikeEvent& a, const SpikeEvent& b) { return a.time < b.time; });

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString reason = file.errorString();
        setStatusText(i18n_->text(QStringLiteral("app.spike.export.failed"),
                                  {{"reason", reason}}));
        return false;
    }
    QTextStream out(&file);
    out << "cluster_id,time_s\n";
    for (const auto& ev : events) {
        out << ev.clusterId << ',' << ev.time << '\n';
    }
    out.flush();
    file.close();

    setStatusText(i18n_->text(QStringLiteral("app.spike.export.completed"),
                              {{"rows", static_cast<int>(events.size())},
                               {"path", outputPath}}));
    return true;
}
}  // namespace bf
