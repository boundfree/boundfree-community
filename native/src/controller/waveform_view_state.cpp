#include "controller/waveform_view_state.h"

#include <algorithm>

namespace bf {

void WaveformViewState::setWindowSeconds(double value) {
    windowSeconds = std::clamp(value, 1.0, 120.0);
}

void WaveformViewState::setAmplitudeUv(double value) {
    amplitudeUv = std::clamp(value, 50.0, 5000.0);
}

void WaveformViewState::setFirstVisibleChannel(int value) {
    firstVisibleChannel = std::max(0, value);
}

void WaveformViewState::setChannelVisible(int channel, bool visible) {
    if (channel < 0) {
        return;
    }
    if (visible) {
        hiddenChannels.erase(channel);
    } else {
        hiddenChannels.insert(channel);
    }
}

bool WaveformViewState::isChannelVisible(int channel) const {
    return channel >= 0 && hiddenChannels.find(channel) == hiddenChannels.end();
}

QString formatVisibleChannels(const WaveformViewState& state, int nChannels) {
    QStringList parts;
    for (int channel = 0; channel < nChannels; ++channel) {
        if (state.isChannelVisible(channel)) {
            parts.push_back(QString::number(channel));
        }
    }
    return parts.join(",");
}

QString formatWaveformViewState(const WaveformViewState& state, int nChannels) {
    return QString("window=%1 amplitude=%2 first_channel=%3 visible=%4")
        .arg(state.windowSeconds, 0, 'f', 0)
        .arg(state.amplitudeUv, 0, 'f', 0)
        .arg(state.firstVisibleChannel)
        .arg(formatVisibleChannels(state, nChannels));
}

}  // namespace bf
