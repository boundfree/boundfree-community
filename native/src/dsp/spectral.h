#pragma once

#include <array>
#include <string>
#include <vector>

namespace bf::dsp {

struct Spectrum {
    std::vector<double> frequencies;
    std::vector<double> values;
};

struct Spectrogram {
    std::vector<double> frequencies;
    std::vector<double> times;
    // Frequency-major: values[frequency][time], expressed in dB.
    std::vector<std::vector<double>> values;
};

struct Band {
    const char* key;
    double low;
    double high;
    std::array<int, 4> rgba;
};

const std::array<Band, 5>& eegBands();

Spectrum computeFftAmplitude(const std::vector<double>& signal, double fs);
Spectrum computeWelchPsd(const std::vector<double>& signal, double fs, int nperseg = 0, bool asDb = false);
Spectrogram computeSpectrogram(const std::vector<double>& signal, double fs, int nperseg, int noverlap, double tStart, double fmax);
Spectrum computeCoherence(const std::vector<double>& a, const std::vector<double>& b, double fs, int nperseg, double fmax);
double bandPower(const Spectrum& psd, double low, double high);

}  // namespace bf::dsp
