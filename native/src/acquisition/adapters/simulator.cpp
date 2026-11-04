#include "acquisition/adapters/simulator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace bf::acquisition {
namespace {
constexpr std::size_t kMaxBlockBytes = 1024 * 1024;
DeviceError simError(const char* code) { return {code, {}, std::nullopt, false, "simulator"}; }

bool sameChannel(const ChannelDescriptor& a, const ChannelDescriptor& b) {
    return a.channel_id == b.channel_id && a.name == b.name && a.signal_type == b.signal_type &&
           a.unit == b.unit && a.scale == b.scale && a.offset == b.offset &&
           a.valid_adc_bits == b.valid_adc_bits && a.minimum == b.minimum && a.maximum == b.maximum;
}
long double sampleValue(const SimChannel& channel, const Rate& rate, std::uint64_t index) {
    long double wave = 0;
    switch (channel.waveform) {
    case SimWaveform::Sine: {
        const long double cycles = static_cast<long double>(index) * channel.frequency_hz * rate.denominator / rate.numerator;
        const long double phase = std::fmod(cycles, 1.0L);
        wave = std::sin(phase * 6.283185307179586476925286766559L);
        break;
    }
    case SimWaveform::Step: wave = index >= channel.step_at ? 1.0L : 0.0L; break;
    case SimWaveform::Pulse: wave = index % channel.pulse_period < channel.pulse_width ? 1.0L : 0.0L; break;
    case SimWaveform::IcaMixture: {
        constexpr long double tau = 6.283185307179586476925286766559L;
        const long double t = static_cast<long double>(index) * rate.denominator / rate.numerator;
        const auto oscillation = [&](long double hz) { return std::sin(tau * std::fmod(t * hz, 1.0L)); };
        const auto pulse = [&](long double period, long double center, long double width) {
            const auto x = std::remainder(t - center, period) / width;
            return std::exp(-.5L * x * x);
        };
        const long double brain = 18 * (1 + .15L * oscillation(.31L)) * oscillation(10) + 4 * oscillation(6);
        const long double eye = 85 * pulse(3.7L, 1.1L, .12L);
        const long double muscle = 20 * pulse(2.3L, 1.6L, .22L) *
            (oscillation(43) + .6L * oscillation(67) + .4L * oscillation(83));
        wave = channel.mixing[0] * brain + channel.mixing[1] * eye + channel.mixing[2] * muscle;
        break;
    }
    }
    return static_cast<long double>(channel.offset) + channel.amplitude * wave;
}

template<typename T>
std::vector<T> makeSamples(const SimStream& stream, std::uint64_t first) {
    const auto count = static_cast<std::size_t>(stream.samples_per_step);
    std::vector<T> values(stream.channels.size() * count);
    for (std::size_t ch = 0; ch < stream.channels.size(); ++ch) {
        for (std::size_t i = 0; i < count; ++i) {
            auto value = sampleValue(stream.channels[ch], stream.rate, first + i);
            if (!std::isfinite(value)) throw std::invalid_argument("non-finite simulator sample");
            if constexpr (std::is_integral<T>::value) value = std::round(value);
            long double min = static_cast<long double>(std::numeric_limits<T>::lowest());
            long double max = static_cast<long double>(std::numeric_limits<T>::max());
            if (stream.dtype == DType::Int24) { min = -8388608.0L; max = 8388607.0L; }
            if (value < min || value > max) throw std::invalid_argument("simulator sample out of range");
            values[ch * count + i] = static_cast<T>(value);
        }
    }
    return values;
}

std::shared_ptr<const SampleStorage> makeStorage(const SimStream& stream, std::uint64_t first, const MemoryBudget& budget) {
    switch (stream.dtype) {
    case DType::Int16: return SampleStorage::fromInt16(makeSamples<std::int16_t>(stream, first), budget);
    case DType::Int24:
    case DType::Int32: return SampleStorage::fromInt32(stream.dtype, makeSamples<std::int32_t>(stream, first), budget);
    case DType::Float32: return SampleStorage::fromFloat32(makeSamples<float>(stream, first), budget);
    case DType::Float64: return SampleStorage::fromFloat64(makeSamples<double>(stream, first), budget);
    }
    throw std::invalid_argument("unsupported simulator dtype");
}

SignalEvent makeGapEvent(const StreamDescriptor& stream, std::uint64_t index, std::uint64_t count, const char* reason) {
    SignalEvent event;
    event.event_id = newIdentity();
    event.stream = stream.key;
    event.capture_id = stream.capture_id;
    event.generation = stream.generation;
    event.kind = "data.gap";
    event.sample_index = index;
    event.gap = SignalGap{stream.key, index, count, reason};
    return event;
}
} // namespace

SimStream icaSimulatorStream(int sampleRate) {
    if (sampleRate != 250 && sampleRate != 500 && sampleRate != 1000)
        throw std::invalid_argument("invalid ICA simulator rate");
    SimStream stream;
    stream.name = "ICA mixture"; stream.signal_type = "eeg";
    stream.rate = {static_cast<std::uint32_t>(sampleRate), 1};
    stream.dtype = DType::Float32; stream.samples_per_step = sampleRate / 50;
    const std::array<std::array<double, 3>, 5> weights = {{{1, .5, .3}, {.3, 1.2, .1}, {1, -.2, 1.1}, {0, 1, 0}, {0, 0, 1}}};
    const std::array<const char*, 5> names = {"Mixed-1", "Mixed-2", "Mixed-3", "Eye-reference", "Muscle-reference"};
    for (int i = 0; i < 5; ++i) {
        SimChannel channel;
        channel.descriptor.channel_id = "sim-ica-v1-" + std::to_string(i + 1);
        channel.descriptor.name = names[i];
        channel.descriptor.signal_type = i == 3 ? "eog" : i == 4 ? "emg" : "eeg";
        channel.descriptor.unit = Unit::Microvolt;
        channel.waveform = SimWaveform::IcaMixture; channel.mixing = weights[i];
        stream.channels.push_back(channel);
    }
    return stream;
}

InProcessSimulator::InProcessSimulator(std::vector<SimStream> config, const MemoryBudget& budget)
    : config_(std::move(config)), budget_(budget) {
    if (config_.empty() || config_.size() > 64) throw std::invalid_argument("invalid simulator stream count");
    for (const auto& stream : config_) {
        const auto item_bytes = inMemoryBytesPerSample(stream.dtype);
        if (!item_bytes || !stream.rate.numerator || !stream.rate.denominator || !stream.samples_per_step ||
            stream.channels.empty() || stream.channels.size() > 4096 || !std::isfinite(stream.clock_drift_ppm) ||
            stream.clock_drift_ppm <= -1000000.0 ||
            stream.channels.size() > kMaxBlockBytes / item_bytes / stream.samples_per_step ||
            stream.channels.size() * item_bytes * stream.samples_per_step > budget_.limit())
            throw std::invalid_argument("invalid simulator stream");
        for (const auto& channel : stream.channels) {
            if (!std::isfinite(channel.amplitude) || !std::isfinite(channel.offset) ||
                !std::isfinite(channel.frequency_hz) || channel.frequency_hz < 0 ||
                !channel.pulse_period || channel.pulse_width > channel.pulse_period ||
                !std::all_of(channel.mixing.begin(), channel.mixing.end(), [](double v) { return std::isfinite(v); }))
                throw std::invalid_argument("invalid simulator waveform");
        }
    }
    device_.device_id = DeviceId{"boundfree.simulator"};
    device_.boot_id = BootId{newIdentity()};
    device_.generated_boot_id = true;
    device_.manufacturer = "BoundFree";
    device_.model = "In-process simulator";
    device_.adapter_version = "1";
    device_.capabilities = {"capture", "synthetic-clock", "fault-injection"};
}
DeviceDescriptor InProcessSimulator::deviceDescriptor() const { return device_; }
std::vector<StreamDescriptor> InProcessSimulator::descriptors() const {
    std::vector<StreamDescriptor> result;
    for (std::size_t i = 0; i < config_.size(); ++i) {
        const auto& stream = config_[i];
        StreamDescriptor descriptor;
        descriptor.name = stream.name;
        descriptor.signal_type = stream.signal_type.empty() ? stream.channels.front().descriptor.signal_type : stream.signal_type;
        descriptor.rate = stream.rate;
        descriptor.dtype = stream.dtype;
        descriptor.clock = {static_cast<std::uint32_t>(i + 1), 1000000000, 1,
                            ClockSource::Estimated, ClockEpoch::SessionRelative, true};
        descriptor.max_samples_per_block = stream.samples_per_step;
        for (const auto& channel : stream.channels) descriptor.channels.push_back(channel.descriptor);
        result.push_back(std::move(descriptor));
    }
    return result;
}
std::optional<DeviceError> InProcessSimulator::prepare(const CaptureDescriptor& capture) {
    if (streaming_ || capture_) return simError("device.busy");
    if (capture.streams.size() != config_.size() || capture.session_id.value.empty() || capture.capture_id.value.empty())
        return simError("acquisition.invalid_capture");
    const auto expected = descriptors();
    std::set<StreamKey> stream_ids;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& a = capture.streams[i];
        const auto& b = expected[i];
        if (validateStreamDescriptor(a) || !stream_ids.insert(a.key).second ||
            a.key.session_id != capture.session_id || a.capture_id != capture.capture_id ||
            a.generation != capture.generation || a.name != b.name || a.signal_type != b.signal_type || a.rate.numerator != b.rate.numerator ||
            a.rate.denominator != b.rate.denominator || a.dtype != b.dtype || a.clock.clock_id != b.clock.clock_id ||
            a.clock.ticks_per_second_num != b.clock.ticks_per_second_num ||
            a.clock.ticks_per_second_den != b.clock.ticks_per_second_den || a.clock.source != b.clock.source ||
            a.clock.epoch != b.clock.epoch || !a.clock.simulated ||
            a.sample_index_source != SampleIndexSource::Device || a.max_samples_per_block != b.max_samples_per_block ||
            a.channels.size() != b.channels.size()) return simError("acquisition.invalid_capture");
        for (std::size_t j = 0; j < a.channels.size(); ++j)
            if (!sameChannel(a.channels[j], b.channels[j])) return simError("acquisition.invalid_capture");
    }
    capture_ = capture;
    last_stopped_.reset();
    next_sample_.assign(config_.size(), 0);
    step_index_ = 0;
    clock_offset_ticks_ = 0;
    return std::nullopt;
}
std::optional<DeviceError> InProcessSimulator::start(const CaptureId& capture_id) {
    if (!capture_ || capture_->capture_id != capture_id) return simError("acquisition.stale_capture");
    streaming_ = true;
    return std::nullopt;
}
std::optional<DeviceError> InProcessSimulator::stop(const CaptureId& capture_id) {
    if (last_stopped_ && *last_stopped_ == capture_id && !capture_) return std::nullopt;
    if (!capture_ || capture_->capture_id != capture_id) return simError("acquisition.stale_capture");
    streaming_ = false;
    last_stopped_ = capture_id;
    capture_.reset();
    return std::nullopt;
}
void InProcessSimulator::setFaults(SimulatorFaults faults) { faults_ = std::move(faults); }
SimulatorStep InProcessSimulator::step() {
    SimulatorStep result;
    if (!streaming_ || !capture_) { result.error = simError("device.invalid_state"); return result; }
    if (step_index_ == std::numeric_limits<std::uint64_t>::max()) {
        streaming_ = false; result.error = simError("acquisition.counter_exhausted"); return result;
    }
    for (std::size_t i = 0; i < config_.size(); ++i) {
        if (next_sample_[i] > std::numeric_limits<std::uint64_t>::max() - config_[i].samples_per_step) {
            streaming_ = false; result.error = simError("acquisition.counter_exhausted"); return result;
        }
    }
    auto jump = faults_.clock_jump_ticks.find(step_index_);
    if (jump != faults_.clock_jump_ticks.end()) {
        const auto delta = jump->second;
        if ((delta > 0 && clock_offset_ticks_ > std::numeric_limits<std::int64_t>::max() - delta) ||
            (delta < 0 && clock_offset_ticks_ < std::numeric_limits<std::int64_t>::min() - delta)) {
            streaming_ = false; result.error = simError("clock.discontinuity"); return result;
        }
        clock_offset_ticks_ += delta;
        for (const auto& stream : capture_->streams) {
            SignalEvent event;
            event.event_id = newIdentity(); event.stream = stream.key; event.capture_id = stream.capture_id;
            event.generation = stream.generation; event.kind = "clock.discontinuity";
            event.params["offset_ticks"] = std::to_string(delta);
            result.events.push_back(std::move(event));
        }
    }
    for (std::size_t i = 0; i < config_.size(); ++i) {
        const auto& stream = config_[i];
        const auto& descriptor = capture_->streams[i];
        const auto first = next_sample_[i];
        next_sample_[i] += stream.samples_per_step;
        if (faults_.drop_steps.count(step_index_)) {
            result.events.push_back(makeGapEvent(descriptor, first, stream.samples_per_step, "simulator.drop"));
            continue;
        }
        const long double ticks = static_cast<long double>(first) * 1000000000.0L * stream.rate.denominator /
                                  stream.rate.numerator * (1.0L + stream.clock_drift_ppm / 1000000.0L) +
                                  static_cast<long double>(clock_offset_ticks_);
        if (ticks < 0 || ticks >= std::ldexp(1.0L, 64)) {
            streaming_ = false;
            result.events.push_back(makeGapEvent(descriptor, first, stream.samples_per_step, "clock.out_of_range"));
            result.error = simError("clock.discontinuity");
            break;
        }
        try {
            const auto scratch_bytes = stream.channels.size() * stream.samples_per_step * inMemoryBytesPerSample(stream.dtype);
            auto scratch = budget_.tryReserve(scratch_bytes);
            if (!scratch) {
                result.events.push_back(makeGapEvent(descriptor, first, stream.samples_per_step, "buffer.budget_exhausted"));
                if (!result.error) result.error = simError("buffer.budget_exhausted");
                continue;
            }
            auto storage = makeStorage(stream, first, budget_);
            if (!storage) {
                result.events.push_back(makeGapEvent(descriptor, first, stream.samples_per_step, "buffer.budget_exhausted"));
                if (!result.error) result.error = simError("buffer.budget_exhausted");
                continue;
            }
            SampleBlock block;
            block.stream = descriptor.key; block.capture_id = capture_->capture_id; block.generation = capture_->generation;
            block.descriptor_revision = descriptor.revision; block.channel_count = static_cast<std::uint32_t>(stream.channels.size());
            block.sample_count = stream.samples_per_step; block.first_sample_index = first;
            block.first_timestamp_ticks = static_cast<std::uint64_t>(ticks); block.clock_id = descriptor.clock.clock_id;
            block.timestamp_estimated = true; block.storage = std::move(storage);
            result.blocks.push_back(block);
            if (faults_.duplicate_steps.count(step_index_)) result.blocks.push_back(std::move(block));
        } catch (const std::bad_alloc&) {
            result.events.push_back(makeGapEvent(descriptor, first, stream.samples_per_step, "buffer.allocation_failed"));
            if (!result.error) result.error = simError("buffer.allocation_failed");
        } catch (const std::invalid_argument&) {
            result.events.push_back(makeGapEvent(descriptor, first, stream.samples_per_step, "simulator.sample_invalid"));
            if (!result.error) result.error = simError("simulator.sample_invalid");
        }
    }
    ++step_index_;
    return result;
}
} // namespace bf::acquisition
