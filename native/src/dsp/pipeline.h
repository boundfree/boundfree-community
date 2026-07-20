#pragma once

#include "dsp/biquad.h"
#include "dsp/filter_design.h"
#include "dsp/reference.h"
#include "dsp/types.h"

#include <optional>
#include <utility>
#include <vector>

namespace bf {
namespace dsp {

class ProcessingPipeline {
public:
    void rebuild(const ProcessingConfig& config);
    const ProcessingConfig* config() const;

    Matrix processRealtime(const Matrix& chunk);
    Matrix processOffline(const Matrix& data) const;

private:
    std::optional<ProcessingConfig> config_;
    std::vector<SosMatrix> notchSosList_;
    // Shared notch filter bank state: one bank applied sequentially.
    struct NotchBank {
        SosMatrix sos;
        SosState state;
    };
    std::vector<NotchBank> notchBanks_;

    struct BandpassState {
        int channel = 0;
        SosMatrix sos;
        std::vector<BiquadState> zi;  // per section for one channel
    };
    std::vector<BandpassState> bandpassStates_;
};

}  // namespace dsp
}  // namespace bf
