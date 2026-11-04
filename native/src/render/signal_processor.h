#pragma once
#include "render/waveform_slice.h"
#include "dsp/pipeline.h"
namespace bf {
// Stateful causal projection of immutable, overlapping raw signal windows.
// It lives on the processing worker, independent of original recording.
class SignalProcessor {
public:
    WaveformSlice process(const WaveformSlice&, const ProcessingViewState&, bool causal);
    void reset();
private:
    QString identity_, configuration_;
    dsp::ProcessingPipeline pipeline_;
    WaveformSlice cached_;
    double lastTime_ = -1;
    bool haveTime_ = false;
};
}
