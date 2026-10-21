#pragma once

#include "ica_core.h"
#include <cstdint>
#include <string>

namespace bf::separation {
enum class Role {
    Unconfirmed, Observation, EogObservation, EmgObservation, EogReference, EmgReference, Excluded
};
struct Request {
    Matrix samples; // channel-major double uV, includes resampling context
    std::vector<Role> roles; // same order as samples
    std::vector<std::string> channelIds; // optional; unique stable IDs when provided
    double sampleRate = 0.0; // source rate
    double outputRate = 500.0; // 250 or 500 Hz, never exceeds source rate
    std::uint64_t readFirstSample = 0; // absolute source index of samples[][0]
    std::uint64_t firstSample = 0; // requested absolute source index
    std::uint64_t sampleCount = 0; // requested source sample count
    std::uint64_t totalSamples = 0; // complete recording sample count
    int notchHz = 0;
    unsigned seed = 42;
    int maxIterations = 600;
    double tolerance = 1e-6;
};
struct ComponentScore {
    double eogCorrelation = 0.0; // max absolute correlation, heuristic, not accuracy
    double emgCorrelation = 0.0;
    double highFrequencyRatio = 0.0; // 30 Hz high-pass energy / 1 Hz high-pass energy
};
struct Result {
    ica::Result ica;
    Matrix original; // anti-aliased observations before optional notch, uV
    std::vector<ComponentScore> scores;
    std::vector<std::string> channelIds; // observation order only
    double sampleRate = 0.0; // output rate
    std::uint64_t firstSample = 0;
    double actualStart = 0.0; // source-file seconds
    int resampleHalfWidth = 0;
};
struct Reconstruction {
    Matrix eeg;
    Matrix eog;
    Matrix emg;
    Matrix unresolved;
    Matrix residual;
    double maxError = 0.0;
};
// Required context on each side, clipped only at actual recording boundaries.
int resamplingHalfWidth(double sourceRate, double outputRate);
Result analyze(const Request& request, Cancel cancel = {}, Progress progress = {});
// No fitting; contributions omit the mean. Identity: analysis = mean + all five matrices.
Reconstruction reconstruct(const Result& result, const std::vector<int>& groups, Cancel cancel = {});
} // namespace bf::separation
