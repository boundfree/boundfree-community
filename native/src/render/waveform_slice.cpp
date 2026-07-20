#include "render/waveform_slice.h"

#include "controller/processing_view_state.h"
#include "dsp/pipeline.h"
#include "dsp/reference.h"

#include <algorithm>

namespace bf {

WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice) {
    WaveformSlice result;
    result.fs = slice.fs;
    result.t0 = slice.t0;
    result.t1 = slice.t1;
    result.unit = slice.unit;
    result.channelNames = session.chNames;
    result.data = slice.data;
    return result;
}

WaveformSlice applyProcessingToSlice(const WaveformSlice& raw, const ProcessingViewState& processing) {
    WaveformSlice out = raw;
    if (raw.data.empty() || raw.fs <= 0.0) {
        return out;
    }

    dsp::ProcessingPipeline pipeline;
    const int nChannels = static_cast<int>(raw.data.size());
    pipeline.rebuild(processing.toConfig(raw.fs, nChannels));
    const dsp::Matrix filtered = pipeline.processOffline(matrixFromFloatChannels(raw.data));
    out.data = floatChannelsFromMatrix(filtered);

    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(raw.channelNames.size()));
    for (const QString& name : raw.channelNames) {
        names.push_back(name.toStdString());
    }
    out.channelNames = qStringListFromStd(
        dsp::effectiveChannelNames(names, processing.referenceMode, static_cast<int>(out.data.size())));
    return out;
}

WaveformDisplayModel waveformDisplayModel(
    const WaveformSlice& slice,
    const WaveformViewState& state,
    int maxVisibleChannels) {
    WaveformDisplayModel model;
    model.fs = slice.fs;
    model.t0 = slice.t0;
    model.t1 = slice.t1;
    model.unit = slice.unit;
    model.amplitudeUv = state.amplitudeUv;

    const int nChannels = static_cast<int>(slice.data.size());
    const int first = std::min(std::max(0, state.firstVisibleChannel), nChannels);
    const int limit = maxVisibleChannels <= 0 ? nChannels : maxVisibleChannels;
    for (int channel = first; channel < nChannels && static_cast<int>(model.data.size()) < limit; ++channel) {
        if (!state.isChannelVisible(channel)) {
            continue;
        }
        model.data.push_back(slice.data[static_cast<std::size_t>(channel)]);
        if (channel < slice.channelNames.size()) {
            model.channelNames.push_back(slice.channelNames[channel]);
        } else {
            model.channelNames.push_back(QString("Ch%1").arg(channel + 1));
        }
    }
    return model;
}

}  // namespace bf
