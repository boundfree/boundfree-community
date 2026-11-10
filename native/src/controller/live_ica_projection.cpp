#include "controller/live_ica_projection.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bf {
bool liveIcaModelCompatible(const SeparationOutput& model, const QString& sourceId) {
    const auto origin = model.manifest.value("source").toObject().value("signal_provenance").toObject();
    return !sourceId.isEmpty() && origin.value("source_id").toString() == sourceId &&
        !origin.value("stream_id").toString().isEmpty() &&
        model.manifest.value("notch_hz").toInt(-1) == 0 &&
        (model.fs == 250 || model.fs == 500) &&
        model.manifest.value("source_rate").toDouble() == model.fs;
}

LiveIcaFrame projectLiveIca(const SeparationOutput& model, const std::vector<int>& labels,
                           const WaveformSlice& raw) {
    const auto fail = [] { throw std::invalid_argument("signal.invalid_window"); };
    const auto origin = model.manifest.value("source").toObject().value("signal_provenance").toObject();
    if (!liveIcaModelCompatible(model, raw.sourceId) || raw.displayOnly || raw.fs != model.fs ||
        raw.streamId != origin.value("stream_id").toString() || raw.unit == "mixed" ||
        QJsonArray::fromStringList(raw.channelIds) != origin.value("channel_ids").toArray() ||
        QJsonArray::fromStringList(raw.channelUnits) != origin.value("channel_units").toArray() ||
        raw.unit != model.manifest.value("source").toObject().value("unit").toString() ||
        raw.data.empty() || raw.data.front().empty()) fail();
    const auto ids = model.manifest.value("observation_channel_ids").toArray();
    const auto weights = model.manifest.value("unmixing").toArray();
    const int channels = ids.size(), rank = model.rank;
    if (channels < 2 || channels > 32 || model.channels.size() != channels || rank < 1 || rank > channels ||
        int(labels.size()) != rank || int(model.mean.size()) != channels ||
        int(model.mixing.size()) != channels || weights.size() != rank) fail();
    // The analysis reader assigns file-local column IDs. Resolve those against
    // the complete immutable snapshot metadata, then use the original device
    // column identities in provenance. Names alone cannot establish identity.
    QStringList fileIds;
    for (const auto& id : model.manifest.value("source").toObject().value("channel_ids").toArray()) fileIds << id.toString();
    auto uniqueFileIds = fileIds, uniqueDeviceIds = raw.channelIds;
    if (fileIds.size() != raw.channelIds.size() || fileIds.contains(QString()) || raw.channelIds.contains(QString()) ||
        uniqueFileIds.removeDuplicates() || uniqueDeviceIds.removeDuplicates()) fail();
    std::vector<int> rows;
    for (const auto& id : ids) {
        const auto row = fileIds.indexOf(id.toString());
        if (row < 0 || row >= int(raw.data.size()) || std::find(rows.begin(), rows.end(), row) != rows.end()) fail();
        rows.push_back(row);
    }
    SeparationMatrix unmixing(rank);
    for (int k = 0; k < rank; ++k) {
        if (labels[k] < 0 || labels[k] > 3 || !weights[k].isArray() || weights[k].toArray().size() != channels) fail();
        for (const auto& v : weights[k].toArray()) {
            if (!v.isDouble() || !std::isfinite(v.toDouble())) fail();
            unmixing[k].push_back(v.toDouble());
        }
    }
    for (int c = 0; c < channels; ++c) {
        if (!std::isfinite(model.mean[c]) || int(model.mixing[c].size()) != rank) fail();
        for (double v : model.mixing[c]) if (!std::isfinite(v)) fail();
    }
    const auto samples = raw.data.front().size();
    if (raw.channelIds.size() != int(raw.data.size()) || raw.channelUnits.size() != int(raw.data.size()) ||
        (!raw.sampleTimes.empty() && raw.sampleTimes.size() != raw.data.size())) fail();
    for (size_t c = 0; c < raw.data.size(); ++c)
        if (raw.data[c].size() != samples || (!raw.sampleTimes.empty() && raw.sampleTimes[c].size() != samples)) fail();
    const auto time = [&](size_t i) { return raw.sampleTimes.empty() ? raw.t0 + i / raw.fs : raw.sampleTimes[0][i]; };
    const double first = time(0), last = time(samples - 1);
    if (!std::isfinite(first) || !std::isfinite(last) || last < first || last - first > 10.01) fail();
    const size_t count = size_t(std::llround((last - first) * raw.fs)) + 1;
    if (count > size_t(raw.fs * 10 + 2)) fail();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    LiveIcaFrame frame;
    auto& out = frame.signal;
    out.fs = raw.fs; out.start = first; out.channels = model.channels; out.rank = rank;
    out.analysis.assign(channels, std::vector<double>(count, nan));
    out.sources.assign(rank, std::vector<double>(count, nan));
    out.residual.assign(channels, std::vector<double>(count, nan)); out.mean = model.mean;
    for (auto& group : frame.groups) group.assign(channels, std::vector<double>(count, nan));
    std::vector<bool> blocked(count), present(count);
    double previous = first;
    for (size_t i = 0; i < samples; ++i) {
        const double t = time(i), position = (t - first) * raw.fs;
        if (!std::isfinite(t) || t < previous - 1e-9 || std::abs(position - std::round(position)) > 1e-4 ||
            position < 0 || position >= double(count)) fail();
        previous = t;
        const size_t at = size_t(std::llround(position));
        bool valid = true;
        for (int row : rows) {
            if (!raw.sampleTimes.empty() && std::abs(raw.sampleTimes[row][i] - t) > 1e-9) fail();
            valid = valid && std::isfinite(raw.data[row][i]);
        }
        if (!valid) { blocked[at] = true; frame.hasGaps = true; continue; }
        if (blocked[at]) continue;
        present[at] = true;
        for (int c = 0; c < channels; ++c) out.analysis[c][at] = raw.data[rows[c]][i];
    }
    for (size_t t = 0; t < count; ++t) {
        if (!present[t] || blocked[t]) {
            frame.hasGaps = true;
            for (auto& channel : out.analysis) channel[t] = nan;
            continue;
        }
        for (int k = 0; k < rank; ++k) {
            double value = 0;
            for (int c = 0; c < channels; ++c) value += unmixing[k][c] * (out.analysis[c][t] - model.mean[c]);
            if (!std::isfinite(value)) fail();
            out.sources[k][t] = value;
        }
        for (int c = 0; c < channels; ++c) {
            double total = 0;
            for (int g = 0; g < 4; ++g) {
                double value = 0;
                for (int k = 0; k < rank; ++k) if (labels[k] == g) value += model.mixing[c][k] * out.sources[k][t];
                if (!std::isfinite(value)) fail();
                frame.groups[g][c][t] = value; total += value;
            }
            out.residual[c][t] = out.analysis[c][t] - model.mean[c] - total;
            if (!std::isfinite(out.residual[c][t])) fail();
        }
    }
    frame.end = first + count / raw.fs;
    return frame;
}
}
