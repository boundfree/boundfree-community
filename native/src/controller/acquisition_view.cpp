#include "controller/acquisition_view.h"
#include "controller/acquisition_format.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace bf {
const char* acquisitionFailureStatusKey(const AcquisitionViewSnapshot& view)
{
    if (view.acquisition == DeviceAcquisitionState::Faulted)
        return view.samples ? "app.device.status_faulted" : "app.device.status_start_failed";
    return "app.device.status_connection_failed";
}
namespace {
using namespace acquisition;
// 799 min/max buckets plus the two window endpoints fit within 1600 points.
constexpr std::size_t kBucketCount = 799;
constexpr std::size_t kMaxDisplayChannels = 16;

QString unitLabel(const std::optional<Unit>& unit) {
    if (!unit) return {};
    switch (*unit) {
    case Unit::Volt: return "V"; case Unit::Millivolt: return "mV"; case Unit::Microvolt: return QString::fromUtf8("µV");
    case Unit::Ampere: return "A"; case Unit::Milliampere: return "mA"; case Unit::Pascal: return "Pa";
    case Unit::Kilopascal: return "kPa"; case Unit::Celsius: return QString::fromUtf8("°C");
    case Unit::Dimensionless: return "1"; case Unit::Count: return "count";
    }
    return {};
}
double sampleAt(const SampleStorage& storage, std::size_t index) {
    if (auto data = storage.data<std::int16_t>()) return (*data)[index];
    if (auto data = storage.data<std::int32_t>()) return (*data)[index];
    if (auto data = storage.data<float>()) return (*data)[index];
    if (auto data = storage.data<double>()) return (*data)[index];
    return 0;
}
struct Bucket {
    bool valid = false;
    bool gap = false;
    std::uint64_t minimumIndex = 0, maximumIndex = 0;
    double minimum = 0, maximum = 0;
};
struct Endpoint {
    bool valid = false;
    std::uint64_t index = 0;
    double value = 0;
};
std::vector<AcquisitionTrace> buildStreamTraces(const StreamDescriptor& descriptor,
        const std::vector<Delivery>& deliveries, std::size_t channelLimit) {
    // This function executes on the acquisition worker. The display budget is
    // independent of block fragmentation and does not constrain the recorder.
    std::vector<AcquisitionTrace> result(std::min(descriptor.channels.size(), channelLimit));
    for (std::size_t c = 0; c < result.size(); ++c) {
        result[c].name = QString::fromStdString(descriptor.channels[c].name);
        result[c].unit = unitLabel(descriptor.channels[c].unit);
        result[c].rate = descriptor.rate;
        result[c].points.reserve(2 * kBucketCount + 2);
    }
    if (result.empty() || !descriptor.rate.numerator || !descriptor.rate.denominator) return result;
    const auto last = std::find_if(deliveries.rbegin(), deliveries.rend(), [&](const auto& delivery) {
        return delivery.block && delivery.block->stream == descriptor.key;
    });
    if (last == deliveries.rend()) return result;
    const double rate = static_cast<double>(descriptor.rate.numerator) / descriptor.rate.denominator;
    const auto& endBlock = *last->block;
    const std::uint64_t end = endBlock.first_sample_index + endBlock.sample_count;
    const auto window = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::ceil(rate * 10.0)));
    const auto begin = end > window ? end - window : 0;
    const auto span = end - begin;
    if (!span) return result;

    // Group by sample index over the entire visible window. A gap within a
    // bucket breaks all lines in that bucket and the following connection;
    // this is conservative even when both extrema lie on opposite sides.
    std::vector<std::array<Bucket, kBucketCount>> buckets(result.size());
    std::vector<Endpoint> firstPoints(result.size()), lastPoints(result.size());
    std::uint64_t previousEnd = 0;
    bool havePrevious = false;
    for (const auto& delivery : deliveries) {
        if (!delivery.block || delivery.block->stream != descriptor.key) continue;
        const auto& block = *delivery.block;
        if (block.first_sample_index + block.sample_count <= begin) continue;
        const bool gap = !delivery.gaps_before.empty() || (havePrevious && previousEnd != block.first_sample_index);
        const auto first = static_cast<std::uint32_t>(begin > block.first_sample_index ? begin - block.first_sample_index : 0);
        for (std::uint32_t i = first; i < block.sample_count; ++i) {
            const auto index = block.first_sample_index + i;
            if (index >= end) break;
            const auto bucketIndex = std::min<std::size_t>(kBucketCount - 1,
                static_cast<std::size_t>((index - begin) * kBucketCount / span));
            for (std::size_t ch = 0; ch < result.size(); ++ch) {
                const double value = sampleAt(*block.storage, ch * block.sample_count + i);
                if (!firstPoints[ch].valid) firstPoints[ch] = {true, index, value};
                lastPoints[ch] = {true, index, value};
                auto& bucket = buckets[ch][bucketIndex];
                if (!bucket.valid) {
                    bucket.valid = true; bucket.minimum = bucket.maximum = value;
                    bucket.minimumIndex = bucket.maximumIndex = index;
                } else {
                    if (value < bucket.minimum) { bucket.minimum = value; bucket.minimumIndex = index; }
                    if (value > bucket.maximum) { bucket.maximum = value; bucket.maximumIndex = index; }
                }
                if (i == first && gap) bucket.gap = true;
            }
        }
        previousEnd = block.first_sample_index + block.sample_count;
        havePrevious = true;
    }
    for (std::size_t ch = 0; ch < result.size(); ++ch) {
        if (!firstPoints[ch].valid) continue;
        const auto firstBucket = std::find_if(buckets[ch].begin(), buckets[ch].end(), [](const auto& b) { return b.valid; });
        const auto lastBucket = std::find_if(buckets[ch].rbegin(), buckets[ch].rend(), [](const auto& b) { return b.valid; });
        const auto add = [&](std::uint64_t index, double raw, bool gap) {
            result[ch].points.push_back({index / rate, acquisition::physicalValue(raw, descriptor.channels[ch]), gap});
        };
        if (firstBucket->minimumIndex != firstPoints[ch].index && firstBucket->maximumIndex != firstPoints[ch].index)
            add(firstPoints[ch].index, firstPoints[ch].value, false);
        bool breakNext = false;
        for (const auto& bucket : buckets[ch]) {
            if (!bucket.valid) continue;
            const bool minimumFirst = bucket.minimumIndex <= bucket.maximumIndex;
            add(minimumFirst ? bucket.minimumIndex : bucket.maximumIndex,
                minimumFirst ? bucket.minimum : bucket.maximum, bucket.gap || breakNext);
            if (bucket.minimumIndex != bucket.maximumIndex)
                add(minimumFirst ? bucket.maximumIndex : bucket.minimumIndex,
                    minimumFirst ? bucket.maximum : bucket.minimum, bucket.gap);
            breakNext = bucket.gap;
        }
        if (lastBucket->minimumIndex != lastPoints[ch].index && lastBucket->maximumIndex != lastPoints[ch].index)
            add(lastPoints[ch].index, lastPoints[ch].value, lastBucket->gap);
    }
    return result;
}
} // namespace

std::vector<AcquisitionTrace> buildAcquisitionTraces(const acquisition::StreamDescriptor& descriptor,
        const std::shared_ptr<acquisition::SignalConsumer>& history) {
    const auto deliveries = history ? history->snapshot() : std::vector<acquisition::Delivery>{};
    return buildStreamTraces(descriptor, deliveries, kMaxDisplayChannels);
}

std::vector<AcquisitionTrace> buildAcquisitionTraces(const acquisition::CaptureDescriptor& capture,
        const std::shared_ptr<acquisition::SignalConsumer>& history) {
    const auto deliveries = history ? history->snapshot() : std::vector<acquisition::Delivery>{};
    std::vector<AcquisitionTrace> result;
    result.reserve(kMaxDisplayChannels);
    for (const auto& descriptor : capture.streams) {
        if (result.size() == kMaxDisplayChannels) break;
        auto traces = buildStreamTraces(descriptor, deliveries, kMaxDisplayChannels - result.size());
        if (capture.streams.size() > 1) {
            const QString streamName = QString::fromStdString(descriptor.name);
            const QString prefix = streamName.isEmpty() ? QStringLiteral("#%1").arg(descriptor.key.stream_id)
                : QStringLiteral("%1 [%2]").arg(streamName).arg(descriptor.key.stream_id);
            for (auto& trace : traces) {
                trace.name = prefix + QStringLiteral(" / ") + trace.name;
                trace.multipleStreams = true;
            }
        }
        result.insert(result.end(), std::make_move_iterator(traces.begin()), std::make_move_iterator(traces.end()));
    }
    return result;
}

std::optional<AcquisitionTraceTimeRange> acquisitionTraceTimeRange(const std::vector<AcquisitionTrace>& traces) {
    std::optional<AcquisitionTraceTimeRange> range;
    for (const auto& trace : traces) {
        if (trace.points.empty()) continue;
        const double first = trace.points.front().seconds;
        const double last = trace.points.back().seconds;
        if (!std::isfinite(first) || !std::isfinite(last) || last < first) continue;
        if (!range) range = AcquisitionTraceTimeRange{first, last};
        else { range->first = std::min(range->first, first); range->last = std::max(range->last, last); }
    }
    return range;
}

WaveformSlice acquisitionSignalWindow(const acquisition::CaptureDescriptor& capture,
        const std::shared_ptr<acquisition::SignalConsumer>& history, const QString& stream, double seconds) {
    if (!history || capture.streams.empty()) throw std::invalid_argument("signal.no_samples");
    auto selected=capture.streams.begin();
    const auto key=[](const auto& d){return QString::fromStdString(d.key.session_id.value)+"/"+QString::number(d.key.stream_id);};
    if(!stream.isEmpty()) selected=std::find_if(capture.streams.begin(),capture.streams.end(),[&](const auto& d){return key(d)==stream;});
    if(selected==capture.streams.end()) selected=capture.streams.begin();
    const auto& d=*selected; const auto deliveries=history->snapshot();
    const auto last=std::find_if(deliveries.rbegin(),deliveries.rend(),[&](const auto& v){return v.block && v.block->stream==d.key;});
    if(last==deliveries.rend())throw std::invalid_argument("signal.no_samples");
    WaveformSlice out;out.fs=double(d.rate.numerator)/d.rate.denominator;
    out.sourceId=QString::fromStdString(capture.capture_id.value)+"/"+QString::number(capture.generation);
    out.streamId=key(d);out.simulated=d.clock.simulated;
    out.metadata=QJsonObject{{"original_dtype",acquisition_io::dtypeName(d.dtype)},{"projection_dtype","float32"},
        {"clock_id",int(d.clock.clock_id)},{"clock_source",d.clock.source==acquisition::ClockSource::Hardware?"hardware":d.clock.source==acquisition::ClockSource::HostMonotonic?"host_monotonic":"estimated"},
        {"sample_index_source",d.sample_index_source==acquisition::SampleIndexSource::Device?"device":"adapter_received"},
        {"time_basis","sample_index_at_declared_rate"},{"simulated",d.clock.simulated}};
    if (d.clock.simulated) {
        const auto profile = capture.metadata.find("simulator_signal_profile");
        if (profile != capture.metadata.end())
            out.metadata.insert("simulator_signal_profile", QString::fromStdString(profile->second));
    }
    const auto end=last->block->first_sample_index+last->block->sample_count;
    const double budgetSeconds=(28.*1024*1024)/(std::max<std::size_t>(1,d.channels.size())*12*out.fs);
    if(seconds>budgetSeconds)throw std::invalid_argument("signal.window_too_large");
    const double context=std::max(seconds,std::min(10.,budgetSeconds));
    const auto count=std::uint64_t(std::ceil(out.fs*context));
    const auto begin=end>count?end-count:0;
    out.t0=begin/out.fs;out.t1=end/out.fs;
    out.data.resize(d.channels.size());out.sampleTimes.resize(d.channels.size());
    for(const auto& ch:d.channels){out.channelNames<<QString::fromStdString(ch.name);out.channelIds<<QString::fromStdString(ch.channel_id);out.channelUnits<<unitLabel(ch.unit);}
    std::uint64_t previous=0;bool have=false;std::size_t points=0;
    for(const auto& delivery:deliveries){
        if(!delivery.block || delivery.block->stream!=d.key)continue;
        const auto& b=*delivery.block;
        if(b.first_sample_index+b.sample_count<=begin)continue;
        const auto first=std::uint32_t(begin>b.first_sample_index?begin-b.first_sample_index:0);
        const bool gap=have && (previous!=b.first_sample_index || !delivery.gaps_before.empty());
        if(gap)for(std::size_t c=0;c<d.channels.size();++c){out.data[c].push_back(std::numeric_limits<float>::quiet_NaN());out.sampleTimes[c].push_back((b.first_sample_index+first)/out.fs);}
        points+=b.sample_count-first+(gap?1:0);
        if(points*(d.channels.size()*sizeof(float)+d.channels.size()*sizeof(double))>32*1024*1024)throw std::invalid_argument("signal.window_too_large");
        for(std::size_t c=0;c<d.channels.size();++c)for(std::uint32_t i=first;i<b.sample_count;++i){
            out.data[c].push_back(acquisition::physicalValue(sampleAt(*b.storage,c*b.sample_count+i),d.channels[c]));
            out.sampleTimes[c].push_back((b.first_sample_index+i)/out.fs);
        }
        previous=b.first_sample_index+b.sample_count;have=true;
    }
    return normalizedSignalWindow(out);
}

WaveformSlice acquisitionWaveformSlice(const std::vector<AcquisitionTrace>& traces) {
    WaveformSlice slice;
    if (const auto range = acquisitionTraceTimeRange(traces)) {
        slice.t0 = range->first;
        slice.t1 = range->last > range->first ? range->last : range->first + 1.0;
    } else slice.t1 = 10.0;
    slice.unit.clear();
    slice.displayOnly = true;
    for (const auto& trace : traces) {
        slice.channelNames.push_back(trace.name); slice.channelUnits.push_back(trace.unit);
        auto& values = slice.data.emplace_back();
        auto& times = slice.sampleTimes.emplace_back();
        for (const auto& point : trace.points) {
            if (!std::isfinite(point.seconds)) continue;
            if (point.gapBefore && !values.empty()) {
                times.push_back(point.seconds); values.push_back(std::numeric_limits<float>::quiet_NaN());
            }
            times.push_back(point.seconds); values.push_back(static_cast<float>(point.value));
        }
    }
    return slice;
}
} // namespace bf
