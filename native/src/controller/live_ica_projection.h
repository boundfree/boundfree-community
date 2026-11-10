#pragma once
#include "controller/eeg_separation_controller.h"
#include "render/waveform_slice.h"

namespace bf {
// A fixed, reviewed calibration is applied to raw samples. Calibration arrays
// stay immutable; live frames never replace the offline result or its export.
struct LiveIcaFrame {
    SeparationOutput signal;
    std::array<SeparationMatrix, 4> groups;
    double end = 0;
    bool hasGaps = false;
};
bool liveIcaModelCompatible(const SeparationOutput& model, const QString& sourceId);
LiveIcaFrame projectLiveIca(const SeparationOutput& model, const std::vector<int>& labels,
                           const WaveformSlice& raw);
}
