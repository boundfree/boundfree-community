#include "controller/spike_analysis_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <unordered_map>

namespace bf {
namespace {

// 后台计算：detectSpikes → sortSpikes → 分桶 → 均值 → 组装 SpikeAnalysis。
// 抛 std::exception 表示失败（code 由 what() 前缀判定）。
dsp::SpikeAnalysis calculate(const SpikeAnalysisRequest& req,
                             const std::shared_ptr<std::atomic<int>>& current,
                             int generation) {
    if (req.signal.empty() || req.fs <= 0.0) {
        throw std::invalid_argument("analysis.invalid_input");
    }

    const auto cancelled = [&]() { return current->load() != generation; };

    // 1) 阈值检测 + 波形提取
    dsp::DetectedSpikes detected = dsp::detectSpikes(req.signal, req.detect);
    if (cancelled()) {
        return {};
    }

    dsp::SpikeAnalysis result;
    result.fs = req.fs;
    result.duration = req.duration > 0.0 ? req.duration
                                         : static_cast<double>(req.signal.size()) / req.fs;
    result.snippetLen = detected.snippetLen;
    result.preMs = req.detect.preMs;
    result.postMs = req.detect.postMs;
    result.channel = req.channel;
    result.totalSpikes = static_cast<int>(detected.timestamps.size());

    // 无 spike：返回空 clusters 的 SpikeAnalysis（不算错误）。
    if (detected.timestamps.empty()) {
        return result;
    }

    // 2) 排序（PCA + K-Means）
    const dsp::SpikeSortResult sorted = dsp::sortSpikes(detected.waveforms, req.sort);
    if (cancelled()) {
        return {};
    }

    // 3) 按 labels 分桶为 clusters
    const int nSpikes = static_cast<int>(detected.timestamps.size());
    const std::vector<int>& labels = sorted.labels;

    // 簇号 → 在 result.clusters 中的下标；保持簇号紧凑且升序。
    std::unordered_map<int, int> clusterIndex;
    clusterIndex.reserve(static_cast<std::size_t>(req.sort.nClusters));
    for (int i = 0; i < nSpikes; ++i) {
        const int label = (i < static_cast<int>(labels.size())) ? labels[static_cast<std::size_t>(i)] : 0;
        if (clusterIndex.find(label) == clusterIndex.end()) {
            clusterIndex.emplace(label, static_cast<int>(result.clusters.size()));
            dsp::SpikeCluster c;
            c.id = label;
            result.clusters.push_back(std::move(c));
        }
    }

    // 收集 timestamps + waveforms
    for (int i = 0; i < nSpikes; ++i) {
        const int label = (i < static_cast<int>(labels.size())) ? labels[static_cast<std::size_t>(i)] : 0;
        const int idx = clusterIndex.at(label);
        dsp::SpikeCluster& c = result.clusters[static_cast<std::size_t>(idx)];
        c.timestamps.push_back(detected.timestamps[static_cast<std::size_t>(i)]);
        c.waveforms.push_back(detected.waveforms[static_cast<std::size_t>(i)]);
    }

    // 4) 每簇计算 meanWaveform（逐样本平均）
    const int snippetLen = detected.snippetLen;
    for (dsp::SpikeCluster& c : result.clusters) {
        if (snippetLen <= 0 || c.waveforms.empty()) {
            continue;
        }
        c.meanWaveform.assign(static_cast<std::size_t>(snippetLen), 0.0);
        for (const std::vector<double>& wf : c.waveforms) {
            const int len = std::min(snippetLen, static_cast<int>(wf.size()));
            for (int s = 0; s < len; ++s) {
                c.meanWaveform[static_cast<std::size_t>(s)] += wf[static_cast<std::size_t>(s)];
            }
        }
        const double inv = 1.0 / static_cast<double>(c.waveforms.size());
        for (double& v : c.meanWaveform) {
            v *= inv;
        }
    }

    return result;
}

}  // namespace

class SpikeAnalysisService::State {
public:
    std::shared_ptr<std::atomic<int>> current = std::make_shared<std::atomic<int>>(0);
    std::atomic<int> next{0};
};

SpikeAnalysisService::SpikeAnalysisService(QObject* parent)
    : QObject(parent), state_(std::make_shared<State>()) {
    qRegisterMetaType<bf::dsp::SpikeAnalysis>("bf::dsp::SpikeAnalysis");
}

SpikeAnalysisService::~SpikeAnalysisService() {
    cancel();
}

int SpikeAnalysisService::request(const SpikeAnalysisRequest& req) {
    const int generation = state_->next.fetch_add(1) + 1;
    state_->current->store(generation);
    const std::shared_ptr<State> state = state_;
    QPointer<SpikeAnalysisService> service(this);
    QThreadPool::globalInstance()->start([service, state, req, generation]() {
        try {
            dsp::SpikeAnalysis result = calculate(req, state->current, generation);
            if (state->current->load() != generation || service.isNull()) {
                return;
            }
            QMetaObject::invokeMethod(service.data(), [service, state, generation, result = std::move(result)]() {
                if (!service.isNull() && state->current->load() == generation) {
                    emit service->completed(generation, result);
                }
            }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            if (state->current->load() != generation || service.isNull()) {
                return;
            }
            const QString detail = QString::fromUtf8(ex.what());
            const QString code = detail.startsWith("analysis.") ? detail : QStringLiteral("analysis.failed");
            QMetaObject::invokeMethod(service.data(), [service, state, generation, code, detail]() {
                if (!service.isNull() && state->current->load() == generation) {
                    emit service->failed(generation, code, detail);
                }
            }, Qt::QueuedConnection);
        }
    });
    return generation;
}

void SpikeAnalysisService::cancel() {
    state_->current->store(state_->next.fetch_add(1) + 1);
}

bool SpikeAnalysisService::isCurrent(int generation) const {
    return state_->current->load() == generation;
}

}  // namespace bf
