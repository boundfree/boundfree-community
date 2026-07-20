#include "controller/processing_view_state.h"

#include "dsp/filter_design.h"
#include "dsp/reference.h"

#include <QStringList>

namespace bf {

dsp::ProcessingConfig ProcessingViewState::toConfig(double fs, int nChannels) const {
    dsp::ProcessingConfig cfg;
    cfg.fs = fs;
    cfg.nChannels = nChannels;
    cfg.referenceMode = referenceMode;

    if (notchEnabled) {
        std::vector<double> fundamentals;
        for (const NotchBandUi& band : notchBands) {
            if (band.enabled && band.frequency > 0.0) {
                fundamentals.push_back(band.frequency);
            }
        }
        cfg.notchFreqs = dsp::expandNotchFrequencies(
            fundamentals, fs, notchHarmonics, notchHarmonicMultiple);
    }

    cfg.bandpass.resize(static_cast<std::size_t>(nChannels));
    for (int ch = 0; ch < nChannels; ++ch) {
        cfg.bandpass[static_cast<std::size_t>(ch)] = dsp::ChannelBandpassSetting{
            globalBandpassEnabled,
            bandpassLow,
            bandpassHigh,
        };
    }
    return cfg;
}

QString ProcessingViewState::summary() const {
    QStringList notchParts;
    if (notchEnabled) {
        for (const NotchBandUi& band : notchBands) {
            if (band.enabled) {
                notchParts.push_back(QString::number(band.frequency));
            }
        }
    }
    return QString("ref=%1 notch=%2 harm=%3 bp=%4")
        .arg(QString::fromUtf8(dsp::referenceModeToString(referenceMode)))
        .arg(notchParts.isEmpty() ? QString("off") : notchParts.join("/"))
        .arg(notchEnabled && notchHarmonics ? QString::number(notchHarmonicMultiple) : QString("off"))
        .arg(globalBandpassEnabled
                 ? QString("%1-%2").arg(bandpassLow).arg(bandpassHigh)
                 : QString("off"));
}

dsp::Matrix matrixFromFloatChannels(const std::vector<std::vector<float>>& data) {
    dsp::Matrix out(data.size());
    for (std::size_t ch = 0; ch < data.size(); ++ch) {
        out[ch].assign(data[ch].begin(), data[ch].end());
    }
    return out;
}

std::vector<std::vector<float>> floatChannelsFromMatrix(const dsp::Matrix& data) {
    std::vector<std::vector<float>> out(data.size());
    for (std::size_t ch = 0; ch < data.size(); ++ch) {
        out[ch].resize(data[ch].size());
        for (std::size_t s = 0; s < data[ch].size(); ++s) {
            out[ch][s] = static_cast<float>(data[ch][s]);
        }
    }
    return out;
}

QStringList qStringListFromStd(const std::vector<std::string>& names) {
    QStringList out;
    out.reserve(static_cast<int>(names.size()));
    for (const std::string& name : names) {
        out.push_back(QString::fromStdString(name));
    }
    return out;
}

}  // namespace bf
