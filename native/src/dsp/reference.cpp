#include "dsp/reference.h"

#include <stdexcept>

namespace bf {
namespace dsp {

Matrix applyReference(const Matrix& data, ReferenceMode mode) {
    if (mode == ReferenceMode::Raw) {
        return cloneMatrix(data);
    }

    if (data.empty()) {
        return {};
    }

    const std::size_t nChannels = data.size();
    const std::size_t nSamples = data.front().size();
    for (const auto& row : data) {
        if (row.size() != nSamples) {
            throw std::invalid_argument("applyReference: jagged matrix");
        }
    }

    if (mode == ReferenceMode::Car) {
        Matrix out = makeMatrix(nChannels, nSamples);
        for (std::size_t s = 0; s < nSamples; ++s) {
            double mean = 0.0;
            for (std::size_t ch = 0; ch < nChannels; ++ch) {
                mean += data[ch][s];
            }
            mean /= static_cast<double>(nChannels);
            for (std::size_t ch = 0; ch < nChannels; ++ch) {
                out[ch][s] = data[ch][s] - mean;
            }
        }
        return out;
    }

    if (mode == ReferenceMode::Bipolar) {
        if (nChannels < 2) {
            return cloneMatrix(data);
        }
        Matrix out = makeMatrix(nChannels - 1, nSamples);
        for (std::size_t ch = 0; ch + 1 < nChannels; ++ch) {
            for (std::size_t s = 0; s < nSamples; ++s) {
                out[ch][s] = data[ch][s] - data[ch + 1][s];
            }
        }
        return out;
    }

    throw std::invalid_argument("applyReference: unsupported mode");
}

std::vector<std::string> effectiveChannelNames(
    const std::vector<std::string>& channelNames,
    ReferenceMode mode,
    int nOut) {
    std::vector<std::string> names;
    if (mode == ReferenceMode::Bipolar && channelNames.size() >= 2) {
        names.reserve(channelNames.size() - 1);
        for (std::size_t i = 0; i + 1 < channelNames.size(); ++i) {
            names.push_back(channelNames[i] + "-" + channelNames[i + 1]);
        }
    } else {
        names = channelNames;
    }
    if (nOut >= 0 && static_cast<int>(names.size()) > nOut) {
        names.resize(static_cast<std::size_t>(nOut));
    }
    return names;
}

}  // namespace dsp
}  // namespace bf
