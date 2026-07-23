#pragma once

#include "dsp/spike_detect.h"
#include "dsp/spike_sort.h"
#include "dsp/spike_types.h"

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

namespace bf {

// Spike 分析请求。signal 为单通道放电迹，fs 由调用方传入（禁止硬编码）。
struct SpikeAnalysisRequest {
    std::vector<double> signal;
    double fs = 0.0;
    double duration = 0.0;
    dsp::SpikeDetectConfig detect;
    dsp::SpikeSortConfig sort;
    int channel = 0;
};

// Spike 分析服务：编排 detectSpikes → sortSpikes → 组装 bf::dsp::SpikeAnalysis。
// 后台线程计算，latest-request-wins（过期 generation 的结果被丢弃）。
class SpikeAnalysisService final : public QObject {
    Q_OBJECT

public:
    explicit SpikeAnalysisService(QObject* parent = nullptr);
    ~SpikeAnalysisService() override;

    // 返回本次请求的 generation；后续 isCurrent(generation) 判定是否仍最新。
    int request(const SpikeAnalysisRequest& req);
    void cancel();
    bool isCurrent(int generation) const;

signals:
    void completed(int generation, const dsp::SpikeAnalysis& result);
    void failed(int generation, const QString& code, const QString& detail);

private:
    class State;
    std::shared_ptr<State> state_;
};

}  // namespace bf

Q_DECLARE_METATYPE(bf::dsp::SpikeAnalysis)
