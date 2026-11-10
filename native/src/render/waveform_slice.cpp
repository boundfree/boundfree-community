#include "render/waveform_slice.h"

#include "render/processing_view_state.h"
#include "dsp/pipeline.h"
#include "dsp/reference.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace bf {

WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice) {
    WaveformSlice result;
    result.fs = slice.fs;
    result.t0 = slice.t0;
    result.t1 = slice.t1;
    result.unit = slice.unit;
    result.channelNames = session.chNames;
    result.data = slice.data;
    result.channelUnits = slice.channelUnits;
    for(const auto& id:session.capture.value("channel_ids").toArray())result.channelIds<<id.toString();
    if(result.channelIds.isEmpty())for(int c=0;c<session.chNames.size();++c)result.channelIds<<session.path+"/"+QString::number(c);
    result.sourceId=session.path;result.streamId=session.capture.value("stream_key").toString();
    result.metadata=session.signalMetadata;
    for(auto i=session.capture.begin();i!=session.capture.end();++i)result.metadata.insert(i.key(),i.value());
    result.simulated=result.metadata.value("simulated").toBool();result.metadata.insert("format",session.format);
    result.metadata.insert("source_unit",session.unit);result.metadata.insert("projection_dtype","float32");
    if (!slice.sampleTimes.empty()) result.sampleTimes.assign(result.data.size(), slice.sampleTimes);
    return result;
}

void validateSignalProcessing(const WaveformSlice& signal,const ProcessingViewState& settings){
    if(signal.displayOnly || !(signal.fs>0) || !std::isfinite(signal.fs))throw std::invalid_argument("signal.invalid_window");
    if(settings.notchEnabled)for(const auto& band:settings.notchBands)
        if(band.enabled && (!(band.frequency>0) || band.frequency>=signal.fs/2))throw std::invalid_argument("signal.invalid_filter_parameters");
    const auto config=settings.toConfig(signal.fs,int(signal.data.size()));
    for(const auto& band:config.bandpass)if(band.enabled && (!(band.low>0) || !(band.low<band.high) || band.high>=signal.fs/2))
        throw std::invalid_argument("signal.invalid_filter_parameters");
}

QString signalUnit(const WaveformSlice& signal, int channel) {
    return signal.channelUnits.value(channel, signal.unit);
}

WaveformSlice normalizedSignalWindow(const WaveformSlice& input) {
    auto out = input;
    out.channelUnits.clear();
    for (int c = 0; c < static_cast<int>(out.data.size()); ++c) {
        auto unit = signalUnit(input, c); double factor = 1;
        if (unit == "V") { factor = 1e6; unit = "uV"; }
        else if (unit == "mV") { factor = 1e3; unit = "uV"; }
        else if (unit == QString::fromUtf8("µV") || unit == QString::fromUtf8("μV")) unit = "uV";
        else if (unit == QString::fromUtf8("°C")) unit = "degC";
        if (factor != 1) for (auto& value : out.data[c]) value = static_cast<float>(value * factor);
        out.channelUnits.push_back(unit);
    }
    out.unit = out.channelUnits.isEmpty() ? input.unit : out.channelUnits.front();
    for (const auto& unit : out.channelUnits) if (unit != out.unit) { out.unit = "mixed"; break; }
    return out;
}

namespace {
double coordinate(const WaveformSlice& s, int c, std::size_t i) {
    if (!s.sampleTimes.empty()) return c < static_cast<int>(s.sampleTimes.size()) && i < s.sampleTimes[c].size()
        ? s.sampleTimes[c][i] : std::numeric_limits<double>::quiet_NaN();
    return s.fs > 0 ? s.t0 + i / s.fs : std::numeric_limits<double>::quiet_NaN();
}
}

WaveformSlice signalWindowRange(const WaveformSlice& signal, double start, double end) {
    auto out = signal; out.t0 = start; out.t1 = end;
    out.data.clear(); out.sampleTimes.clear();
    for (int c = 0; c < static_cast<int>(signal.data.size()); ++c) {
        auto& values = out.data.emplace_back(); auto& times = out.sampleTimes.emplace_back();
        const auto& data = signal.data[c];
        for (std::size_t i = 0; i < data.size(); ++i) {
            const double time = coordinate(signal, c, i);
            if (time >= start - 1e-9 && time < end - 1e-9) { values.push_back(data[i]); times.push_back(time); }
        }
    }
    return out;
}

std::vector<SignalRange> signalContinuousRanges(const WaveformSlice& signal, const std::vector<int>& channels) {
    std::vector<SignalRange> ranges;
    if (!(signal.fs > 0) || channels.empty()) return ranges;
    std::size_t count = std::numeric_limits<std::size_t>::max();
    for (int c : channels) {
        if (c < 0 || c >= static_cast<int>(signal.data.size())) return ranges;
        count = std::min(count, signal.data[c].size());
    }
    bool active = false; double previous = 0;
    const double step = 1 / signal.fs, tolerance = std::max(1e-9, step * 1e-4);
    for (std::size_t i = 0; i < count; ++i) {
        const double time = coordinate(signal, channels.front(), i);
        bool valid = std::isfinite(time);
        for (int c : channels) valid = valid && std::isfinite(signal.data[c][i]) &&
            std::abs(coordinate(signal, c, i) - time) <= tolerance;
        if (!valid) { active = false; continue; }
        if (!active || std::abs(time - previous - step) > tolerance) {
            ranges.push_back({i, i + 1, time}); active = true;
        } else ranges.back().last = i + 1;
        previous = time;
    }
    return ranges;
}

WaveformSlice applyProcessingToSlice(const WaveformSlice& input, const ProcessingViewState& processing) {
    validateSignalProcessing(input,processing);
    auto raw = normalizedSignalWindow(input), out = raw;
    if (raw.data.empty() || raw.fs <= 0.0) return out;
    const int nChannels = static_cast<int>(raw.data.size());
    const bool reference = processing.referenceMode != dsp::ReferenceMode::Raw;
    if (reference) for (int c = 1; c < nChannels; ++c)
        if (signalUnit(raw,c) != signalUnit(raw,0)) throw std::invalid_argument("signal.incompatible_units");
    const int outputs = processing.referenceMode == dsp::ReferenceMode::Bipolar && nChannels >= 2 ? nChannels - 1 : nChannels;
    out.data.assign(outputs, std::vector<float>(raw.data.front().size(), std::numeric_limits<float>::quiet_NaN()));
    if (reference) {
        std::vector<int> channels(nChannels); std::iota(channels.begin(), channels.end(), 0);
        for (const auto& span : signalContinuousRanges(raw, channels)) {
            dsp::Matrix matrix;
            for (const auto& channel : raw.data) matrix.emplace_back(channel.begin()+span.first, channel.begin()+span.last);
            dsp::ProcessingPipeline pipeline; pipeline.rebuild(processing.toConfig(raw.fs,nChannels));
            dsp::Matrix filtered;
            try{filtered=pipeline.processOffline(matrix);}catch(const std::invalid_argument& e){if(std::string(e.what()).find("padlen too large")!=std::string::npos)continue;throw;}
            for (int c=0;c<outputs;++c) std::copy(filtered[c].begin(),filtered[c].end(),out.data[c].begin()+span.first);
        }
    } else {
        for (int c=0;c<nChannels;++c) for (const auto& span : signalContinuousRanges(raw,{c})) {
            dsp::Matrix matrix(1,std::vector<double>(raw.data[c].begin()+span.first,raw.data[c].begin()+span.last));
            auto config = processing.toConfig(raw.fs,nChannels);
            config.nChannels=1; config.bandpass={config.bandpass[c]};
            dsp::ProcessingPipeline pipeline; pipeline.rebuild(config);
            dsp::Matrix filtered;
            try{filtered=pipeline.processOffline(matrix);}catch(const std::invalid_argument& e){if(std::string(e.what()).find("padlen too large")!=std::string::npos)continue;throw;}
            std::copy(filtered[0].begin(),filtered[0].end(),out.data[c].begin()+span.first);
        }
    }
    std::vector<std::string> names;
    for (const auto& name : raw.channelNames) names.push_back(name.toStdString());
    out.channelNames=qStringListFromStd(dsp::effectiveChannelNames(names,processing.referenceMode,outputs));
    if(outputs<nChannels){out.channelIds.clear();for(int c=0;c<outputs;++c)out.channelIds<<raw.channelIds.value(c)+"-"+raw.channelIds.value(c+1);}
    while (out.channelUnits.size()>outputs) out.channelUnits.removeLast();
    while (out.sampleTimes.size()>static_cast<std::size_t>(outputs)) out.sampleTimes.pop_back();
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
        if (!slice.sampleTimes.empty())
            model.sampleTimes.push_back(channel < static_cast<int>(slice.sampleTimes.size()) ? slice.sampleTimes[channel] : std::vector<double>{});
        if (!slice.channelUnits.isEmpty()) model.channelUnits.push_back(slice.channelUnits.value(channel, slice.unit));
        if (channel < slice.channelNames.size()) {
            model.channelNames.push_back(slice.channelNames[channel]);
        } else {
            model.channelNames.push_back(QString("Ch%1").arg(channel + 1));
        }
    }
    return model;
}

}  // namespace bf
