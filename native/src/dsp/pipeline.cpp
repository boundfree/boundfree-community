#include "dsp/pipeline.h"

#include <stdexcept>

namespace bf {
namespace dsp {

void ProcessingPipeline::rebuild(const ProcessingConfig& config) {
    config_ = config;
    notchSosList_.clear();
    notchBanks_.clear();
    bandpassStates_.clear();

    for (double freq : config.notchFreqs) {
        SosMatrix sos = designNotch(freq, config.fs);
        notchSosList_.push_back(sos);
    }

    int nEff = config.nChannels;
    if (config.referenceMode == ReferenceMode::Bipolar && config.nChannels >= 2) {
        nEff = config.nChannels - 1;
    }

    for (const SosMatrix& sos : notchSosList_) {
        NotchBank bank;
        bank.sos = sos;
        bank.state = makeSteadyStateSosState(sos, nEff);
        notchBanks_.push_back(std::move(bank));
    }

    for (int ch = 0; ch < static_cast<int>(config.bandpass.size()); ++ch) {
        const ChannelBandpassSetting& setting = config.bandpass[static_cast<std::size_t>(ch)];
        if (!setting.enabled || !(setting.low < setting.high)) {
            continue;
        }
        BandpassState state;
        state.channel = ch;
        state.sos = designBandpass(setting.low, setting.high, config.fs);
        // sosfilt_zi for single channel
        SosState ziMat = makeSteadyStateSosState(state.sos, 1);
        state.zi.resize(state.sos.size());
        for (std::size_t sec = 0; sec < state.sos.size(); ++sec) {
            state.zi[sec] = ziMat[sec][0];
        }
        bandpassStates_.push_back(std::move(state));
    }
}

const ProcessingConfig* ProcessingPipeline::config() const {
    return config_ ? &(*config_) : nullptr;
}

Matrix ProcessingPipeline::processRealtime(const Matrix& chunk) {
    if (!config_) {
        return cloneMatrix(chunk);
    }
    Matrix out = applyReference(chunk, config_->referenceMode);
    for (NotchBank& bank : notchBanks_) {
        out = sosFilt(bank.sos, out, &bank.state);
    }
    for (BandpassState& bp : bandpassStates_) {
        if (bp.channel < 0 || static_cast<std::size_t>(bp.channel) >= out.size()) {
            continue;
        }
        std::vector<BiquadState> zi = bp.zi;
        // filter one channel
        Matrix one(1, out[static_cast<std::size_t>(bp.channel)]);
        SosState state(bp.sos.size(), std::vector<BiquadState>(1));
        for (std::size_t sec = 0; sec < bp.sos.size(); ++sec) {
            state[sec][0] = zi[sec];
        }
        Matrix filtered = sosFilt(bp.sos, one, &state);
        out[static_cast<std::size_t>(bp.channel)] = filtered[0];
        for (std::size_t sec = 0; sec < bp.sos.size(); ++sec) {
            bp.zi[sec] = state[sec][0];
        }
    }
    return out;
}

Matrix ProcessingPipeline::processOffline(const Matrix& data) const {
    if (!config_) {
        return cloneMatrix(data);
    }
    Matrix out = applyReference(data, config_->referenceMode);
    if (!notchSosList_.empty()) {
        out = offlineFilter(out, notchSosList_);
    }
    for (int ch = 0; ch < static_cast<int>(config_->bandpass.size()); ++ch) {
        if (ch >= static_cast<int>(out.size())) {
            break;
        }
        const ChannelBandpassSetting& setting = config_->bandpass[static_cast<std::size_t>(ch)];
        if (!setting.enabled || !(setting.low < setting.high)) {
            continue;
        }
        const SosMatrix sos = designBandpass(setting.low, setting.high, config_->fs);
        Matrix one(1, out[static_cast<std::size_t>(ch)]);
        one = sosFiltFilt(sos, one);
        out[static_cast<std::size_t>(ch)] = one[0];
    }
    return out;
}

}  // namespace dsp
}  // namespace bf
