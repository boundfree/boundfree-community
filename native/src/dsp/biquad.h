#pragma once

#include "dsp/types.h"

#include <array>
#include <vector>

namespace bf {
namespace dsp {

// Direct Form II Transposed state for one section / one channel: [s1, s2]
using BiquadState = std::array<double, 2>;

// states[section][channel][2]
using SosState = std::vector<std::vector<BiquadState>>;

SosState makeSosState(const SosMatrix& sos, int nChannels);
SosState makeSteadyStateSosState(const SosMatrix& sos, int nChannels);

// Realtime sosfilt along samples axis for multi-channel data.
Matrix sosFilt(const SosMatrix& sos, const Matrix& data, SosState* state);

// Offline zero-phase sosfiltfilt with scipy-compatible odd padding.
Matrix sosFiltFilt(const SosMatrix& sos, const Matrix& data);

// Apply a list of SOS filters offline in order.
Matrix offlineFilter(const Matrix& data, const std::vector<SosMatrix>& sosList);

}  // namespace dsp
}  // namespace bf
