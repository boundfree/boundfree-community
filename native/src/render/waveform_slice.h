#pragma once

#include "render/processing_view_state.h"
#include "render/waveform_view_state.h"
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
    // Optional explicit coordinates for reduced live envelopes. They preserve
    // each stream's sample-relative times without resampling or synchronization.
    std::vector<std::vector<double>> sampleTimes;
    QStringList channelUnits;
    QJsonObject metadata;
    QStringList channelIds;
    QString sourceId;
    QString streamId;
    bool simulated = false;
    bool displayOnly = false;
};

struct WaveformDisplayModel {
    double fs = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
    double amplitudeUv = 200.0;
    QString unit = "uV";
    QStringList channelNames;
    std::vector<std::vector<float>> data;
    std::vector<std::vector<double>> sampleTimes;
    QStringList channelUnits;
};

WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice);
// Common numerical input contract. Explicit coordinates are never treated as
// uniformly spaced array offsets; display envelopes are not valid inputs here.
struct SignalRange { std::size_t first = 0, last = 0; double start = 0; };
void validateSignalProcessing(const WaveformSlice&, const ProcessingViewState&);
QString signalUnit(const WaveformSlice& signal, int channel);
WaveformSlice normalizedSignalWindow(const WaveformSlice& signal);
WaveformSlice signalWindowRange(const WaveformSlice& signal, double start, double end);
std::vector<SignalRange> signalContinuousRanges(const WaveformSlice& signal, const std::vector<int>& channels);
WaveformSlice applyProcessingToSlice(const WaveformSlice& raw, const ProcessingViewState& processing);
WaveformDisplayModel waveformDisplayModel(
    const WaveformSlice& slice,
    const WaveformViewState& state,
    int maxVisibleChannels);

}  // namespace bf
