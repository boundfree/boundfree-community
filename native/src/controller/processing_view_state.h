#pragma once

#include "dsp/types.h"

#include <QString>
#include <map>
#include <vector>

namespace bf {

struct NotchBandUi {
    bool enabled = false;
    double frequency = 50.0;
};

// Mirrors dsp::ChannelBandpassSetting but lives in the UI/state layer so the
// controller does not depend on dsp:: types for editable overrides. The fields
// are kept identical (enabled/low/high) so toConfig() can copy them directly.
struct ChannelBandpassUi {
    bool enabled = false;
    double low = 0.5;
    double high = 40.0;
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
    // Per-channel bandpass overrides (key = channel index). When a channel is
    // present here, toConfig() uses its enabled/low/high instead of the global
    // fields above; channels absent from the map fall back to global. The map
    // is empty by default so behavior is identical to the previous global-only
    // path until a caller writes an override.
    std::map<int, ChannelBandpassUi> perChannelBandpass;

    dsp::ProcessingConfig toConfig(double fs, int nChannels) const;
    QString summary() const;

    // Write/remove a single per-channel bandpass override.
    void setChannelBandpass(int ch, ChannelBandpassUi setting);
    void clearChannelBandpass(int ch);
    // Remove every per-channel override (return to global-only behavior).
    void clearAllChannelBandpass();
    // Number of channels with an explicit override.
    int channelBandpassOverrideCount() const;
};

dsp::Matrix matrixFromFloatChannels(const std::vector<std::vector<float>>& data);
std::vector<std::vector<float>> floatChannelsFromMatrix(const dsp::Matrix& data);
QStringList qStringListFromStd(const std::vector<std::string>& names);

}  // namespace bf
