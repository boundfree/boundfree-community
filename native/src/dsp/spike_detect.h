#pragma once

#include <vector>

namespace bf::dsp {

// Spike threshold detection and waveform extraction configuration
struct SpikeDetectConfig {
    double fs = 30000.0;            // Sampling rate Hz (no hardcoding, passed by caller)
    double thresholdMad = 4.0;     // N * MAD threshold multiplier (takes effect when useAbsoluteUv=false)
    bool useAbsoluteUv = false;     // If true, use absoluteThresholdUv as threshold
    double absoluteThresholdUv = 50.0; // Absolute threshold (uV)
    bool detectNegative = true;     // true: detect negative peaks when crossing negative threshold; false: detect positive peaks when crossing positive threshold
    double refractoryMs = 1.0;      // Refractory period (dead zone), milliseconds
    double preMs = 0.5;             // Waveform snippet alignment pre-window, milliseconds
    double postMs = 1.0;            // Waveform snippet alignment post-window, milliseconds
};

// Single channel detection results
struct DetectedSpikes {
    std::vector<double> timestamps;              // Seconds (aligned peak times)
    std::vector<std::vector<double>> waveforms;  // [n_spikes][snippet_len]
    int snippetLen = 0;                          // = round((preMs+postMs)/1000*fs)
};

// Single channel threshold detection. Threshold: MAD = median(|x - median(x)|),
// threshold = thresholdMad * (MAD/0.6745) (approximate Gaussian std).
// After crossing the threshold, search for the local extremum in a local window as the aligned peak,
// apply the refractory period, and extract the [peak - preSamples, peak + postSamples) snippet.
// Spikes with insufficient boundary samples are discarded.
DetectedSpikes detectSpikes(const std::vector<double>& signal,
                            const SpikeDetectConfig& cfg);

}  // namespace bf::dsp
