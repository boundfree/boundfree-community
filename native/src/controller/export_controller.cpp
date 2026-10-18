#include "controller/export_controller.h"
#include "render/waveform_slice.h"

#include <QFileInfo>

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
                const auto opened = sidecar.openFile(sourcePath, 30000);
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
                        if (slice.unit != "uV") throw std::invalid_argument("processed export requires uV input");
                        auto filtered = applyProcessingToSlice(waveformSliceFromSidecar(opened, slice), processing->state);
                        // Reader slicing uses Python round (ties to even).
                        const auto sampleIndex = [fs = slice.fs](double time) {
                            return static_cast<qint64>(std::nearbyint(time * fs));
                        };
                        const auto first = sampleIndex(t0) - sampleIndex(contextStart);
                        const auto count = sampleIndex(t1) - sampleIndex(t0);
                        for (auto& channel : filtered.data) {
                            if (first < 0 || count <= 0 || first + count > static_cast<qint64>(channel.size()))
                                throw std::invalid_argument("export range does not contain valid samples");
                            channel = std::vector<float>(channel.begin() + first, channel.begin() + first + count);
                        }
                        result = sidecar.exportSamples(slice.fs, filtered.channelNames, filtered.data, t0, t1, outputPath, events, 60000);
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
