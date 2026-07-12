#pragma once

#include <QString>
#include <QStringList>
#include <set>

namespace bf {

struct WaveformViewState {
    double windowSeconds = 10.0;
    double amplitudeUv = 200.0;
    int firstVisibleChannel = 0;
    std::set<int> hiddenChannels;

    void setWindowSeconds(double value);
    void setAmplitudeUv(double value);
    void setFirstVisibleChannel(int value);
    void setChannelVisible(int channel, bool visible);
    bool isChannelVisible(int channel) const;
};

QString formatVisibleChannels(const WaveformViewState& state, int nChannels);
QString formatWaveformViewState(const WaveformViewState& state, int nChannels);

}  // namespace bf
