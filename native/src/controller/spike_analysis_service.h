#pragma once

#include "dsp/spike_detect.h"
#include "dsp/spike_sort.h"
#include "dsp/spike_types.h"

#include <QObject>
#include <QString>
#include <memory>
#include <vector>

namespace bf {

// Spike analysis request. 'signal' is the single-channel discharge trace, 'fs' is passed by the caller (no hardcoding).
struct SpikeAnalysisRequest {
    std::vector<double> signal;
    double fs = 0.0;
    double duration = 0.0;
    dsp::SpikeDetectConfig detect;
    dsp::SpikeSortConfig sort;
    int channel = 0;
};

// Spike analysis service: orchestrates detectSpikes -> sortSpikes -> assemble bf::dsp::SpikeAnalysis.
// Background thread computation, latest-request-wins (results from expired generation are discarded).
class SpikeAnalysisService final : public QObject {
    Q_OBJECT

public:
    explicit SpikeAnalysisService(QObject* parent = nullptr);
    ~SpikeAnalysisService() override;

    // Returns the generation of this request; subsequent isCurrent(generation) determines if it is still the latest.
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
