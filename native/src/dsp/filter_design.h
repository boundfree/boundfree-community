#pragma once

#include "dsp/types.h"

#include <vector>

namespace bf {
namespace dsp {

// Butterworth bandpass order=4 by default, scipy-compatible SOS.
SosMatrix designBandpass(double low, double high, double fs, int order = 4);

// iirnotch + tf2sos equivalent, default Q=30.
SosMatrix designNotch(double freq, double fs, double q = 30.0);

// Expand fundamental freqs with optional harmonics under Nyquist.
std::vector<double> expandNotchFrequencies(
    const std::vector<double>& fundamentals,
    double fs,
    bool includeHarmonics,
    int maxHarmonicMultiple);

}  // namespace dsp
}  // namespace bf
