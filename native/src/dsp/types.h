#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace bf {
namespace dsp {

// Channel-major matrix: data[channel][sample]
using Matrix = std::vector<std::vector<double>>;

// One SOS section: b0 b1 b2 a0 a1 a2 (a0 expected 1)
using SosSection = std::array<double, 6>;
using SosMatrix = std::vector<SosSection>;

enum class ReferenceMode {
    Raw,
    Car,
    Bipolar,
};

inline ReferenceMode referenceModeFromString(const std::string& mode) {
    if (mode == "car") {
        return ReferenceMode::Car;
    }
    if (mode == "bipolar") {
        return ReferenceMode::Bipolar;
    }
    return ReferenceMode::Raw;
}

inline const char* referenceModeToString(ReferenceMode mode) {
    switch (mode) {
    case ReferenceMode::Car:
        return "car";
    case ReferenceMode::Bipolar:
        return "bipolar";
    case ReferenceMode::Raw:
    default:
        return "raw";
    }
}

struct ChannelBandpassSetting {
    bool enabled = false;
    double low = 0.5;
    double high = 40.0;
};

struct ProcessingConfig {
    double fs = 0.0;
    int nChannels = 0;
    ReferenceMode referenceMode = ReferenceMode::Raw;
    std::vector<double> notchFreqs;
    std::vector<ChannelBandpassSetting> bandpass;
};

inline Matrix makeMatrix(std::size_t channels, std::size_t samples, double fill = 0.0) {
    return Matrix(channels, std::vector<double>(samples, fill));
}

inline Matrix cloneMatrix(const Matrix& in) {
    return in;
}

}  // namespace dsp
}  // namespace bf
