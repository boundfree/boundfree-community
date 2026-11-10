#include "controller/export_controller.h"
#include "controller/sidecar_slice.h"
#include "render/waveform_slice.h"
#include "render/signal_processor.h"

#include <QFileInfo>
#include <QJsonArray>

#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <memory>
#include <utility>

namespace bf {

ExportController::ExportController(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent), repoRoot_(std::move(repoRoot)), version_(std::move(version)) {}

ExportController::~ExportController() {
    if (worker_ != nullptr && worker_->isRunning()) {
        worker_->wait();
    }
}

bool ExportController::exportSignalAsync(const WaveformSlice& signal,const QString& path,const QVector<SidecarEvent>& events,const QJsonObject& provenance){
    if(worker_ || signal.data.empty() || signal.displayOnly || path.isEmpty())return false;
    const QFileInfo source(signal.sourceId),destination(path);
    if(!signal.sourceId.isEmpty() && (source.absoluteFilePath()==destination.absoluteFilePath() ||
       (source.exists() && destination.exists() && source.canonicalFilePath()==destination.canonicalFilePath())))return false;
    QString captureRoot;
    if(source.isDir())captureRoot=source.canonicalFilePath();
    else if((source.fileName()=="signals.h5" || source.fileName()=="manifest.json") && QFileInfo(source.absolutePath()+"/manifest.json").isFile())captureRoot=source.absolutePath();
    if(!captureRoot.isEmpty() && destination.absoluteFilePath().startsWith(captureRoot+"/"))return false;
    const auto outcome=std::make_shared<SidecarExportResult>();const auto root=repoRoot_;const auto version=version_;
    worker_=QThread::create([signal,path,events,provenance,outcome,root,version]{
        SidecarClient sidecar(root);const auto hello=sidecar.startAndHello(version,30000);
        if(!hello.ok){outcome->error=hello.error;return;}
        QJsonArray times;if(!signal.sampleTimes.empty())for(double t:signal.sampleTimes.front())times.append(t);
        QJsonObject meta{{"unit",signal.unit},{"channel_units",QJsonArray::fromStringList(signal.channelUnits)},
            {"provenance",provenance}};
        if(!times.isEmpty())meta.insert("sample_times",times);
        *outcome=sidecar.exportSamples(signal.fs,signal.channelNames,signal.data,signal.t0,signal.t1,path,events,60000,meta);
    });
    connect(worker_,&QThread::finished,this,[this,outcome]{worker_=nullptr;if(outcome->ok)emit completed(*outcome);else emit failed(outcome->error);});
    connect(worker_,&QThread::finished,worker_,&QObject::deleteLater);worker_->start();return true;
}

bool ExportController::exportAsync(
    const QString& sourcePath,
    double t0,
    double t1,
    double duration,
    const QString& outputPath,
    const QVector<SidecarEvent>& events,
    const std::optional<ExportProcessingOptions>& processing) {
    if (worker_ != nullptr || sourcePath.isEmpty() || outputPath.isEmpty() || !std::isfinite(t0) ||
        !std::isfinite(t1) || !std::isfinite(duration) || t0 < 0.0 || t1 <= t0 || t1 > duration ||
        (processing && (!std::isfinite(processing->contextStart) || !std::isfinite(processing->contextEnd)))) {
        return false;
    }
    const QString suffix = QFileInfo(outputPath).suffix().toLower();
    if (suffix != "edf" && suffix != "h5" && suffix != "hdf5" && suffix != "csv") {
        return false;
    }
    const QFileInfo sourceInfo(sourcePath), outputInfo(outputPath);
    if (sourceInfo.absoluteFilePath() == outputInfo.absoluteFilePath() ||
        (outputInfo.exists() && sourceInfo.canonicalFilePath() == outputInfo.canonicalFilePath())) return false;
    const QString repoRoot = repoRoot_;
    const VersionInfo version = version_;
    const auto outcome = std::make_shared<SidecarExportResult>();
    worker_ = QThread::create([outcome, repoRoot, version, sourcePath, t0, t1, outputPath, events, processing]() {
        auto& result = *outcome;
        SidecarClient sidecar(repoRoot);
        const SidecarHelloResult hello = sidecar.startAndHello(version, 5000);
        if (!hello.ok) {
            result.error = hello.error;
            return;
        }
        try {
            if (!processing) {
                result = sidecar.exportSegment(sourcePath, t0, t1, outputPath, events, 60000);
            } else {
                const auto opened = sidecar.openFile(sourcePath, 30000, processing->streamKey);
                if (!opened.ok) {
                    result.error = opened.error;
                } else {
                    if (t1 > opened.duration) throw std::invalid_argument("source duration changed before export");
                    const double contextStart = processing->contextStart >= 0.0
                        ? std::clamp(processing->contextStart, 0.0, t0) : t0;
                    const double contextEnd = std::clamp(std::max(t1, processing->contextEnd), t1, opened.duration);
                    const auto slice = sidecar.getSlice(opened.handle, contextStart, contextEnd, 60000);
                    if (!slice.ok) {
                        result.error = slice.error;
                    } else {
                        auto raw=normalizedSignalWindow(waveformSliceFromSidecar(opened,slice));
                        SignalProcessor processor;
                        auto filtered=processing->applyProcessing?processor.process(raw,processing->state,processing->causal):raw;
                        filtered=signalWindowRange(filtered,t0,t1);
                        QJsonArray times;if(!filtered.sampleTimes.empty())for(double t:filtered.sampleTimes.front())times.append(t);
                        QJsonObject metadata{{"unit",filtered.unit},{"channel_units",QJsonArray::fromStringList(filtered.channelUnits)}};
                        if(!times.isEmpty())metadata.insert("sample_times",times);
                        result=sidecar.exportSamples(filtered.fs,filtered.channelNames,filtered.data,t0,t1,outputPath,events,60000,metadata);
                    }
                }
            }
        } catch (const std::exception& ex) {
            result.error = makeError("export.failed", {{"path", outputPath}, {"reason", QString::fromUtf8(ex.what())}});
        }
        sidecar.stop();
    });
    connect(worker_, &QThread::finished, this, [this, outcome]() {
        // A completion signal also promises that the next export can start.
        worker_ = nullptr;
        if (outcome->ok) emit completed(*outcome);
        else emit failed(outcome->error);
    });
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
    worker_->start();
    return true;
}

}  // namespace bf
