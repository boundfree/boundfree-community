#pragma once

#include "controller/processing_view_state.h"
#include "controller/waveform_view_state.h"
#include "ipc/sidecar_client.h"

#include <QStringList>
#include <vector>

namespace bf {

struct WaveformSlice {
    double fs = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
    QString unit = "uV";
    QStringList channelNames;
    std::vector<std::vector<float>> data;
};

struct WaveformDisplayModel {
    double fs = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
    double amplitudeUv = 200.0;
    QString unit = "uV";
    QStringList channelNames;
    std::vector<std::vector<float>> data;
};

WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice);
WaveformSlice applyProcessingToSlice(const WaveformSlice& raw, const ProcessingViewState& processing);
WaveformDisplayModel waveformDisplayModel(
    const WaveformSlice& slice,
    const WaveformViewState& state,
    int maxVisibleChannels);

}  // namespace bf
