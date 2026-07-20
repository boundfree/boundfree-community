#pragma once

#include "dsp/types.h"

#include <QString>
#include <QStringList>
#include <vector>

namespace bf {

struct NotchBandUi {
    bool enabled = false;
    double frequency = 50.0;
};

struct ProcessingViewState {
    dsp::ReferenceMode referenceMode = dsp::ReferenceMode::Raw;
    bool notchEnabled = false;
    std::vector<NotchBandUi> notchBands = {
        {true, 50.0},
        {false, 100.0},
        {false, 150.0},
    };
    bool notchHarmonics = false;
    int notchHarmonicMultiple = 3;
    bool globalBandpassEnabled = false;
    double bandpassLow = 0.5;
    double bandpassHigh = 40.0;

    dsp::ProcessingConfig toConfig(double fs, int nChannels) const;
    QString summary() const;
};

dsp::Matrix matrixFromFloatChannels(const std::vector<std::vector<float>>& data);
std::vector<std::vector<float>> floatChannelsFromMatrix(const dsp::Matrix& data);
QStringList qStringListFromStd(const std::vector<std::string>& names);

}  // namespace bf
