#include "acquisition/model/signal_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace bf::acquisition {
namespace {
constexpr std::size_t kStorageOverhead = 512;
DeviceError error(const char* code, const char* field) {
    return {code, {{"field", field}}, std::nullopt, false, "model"};
}
bool validUnit(Unit unit) {
    switch (unit) {
    case Unit::Volt: case Unit::Millivolt: case Unit::Microvolt: case Unit::Ampere: case Unit::Milliampere:
    case Unit::Pascal: case Unit::Kilopascal: case Unit::Celsius: case Unit::Dimensionless: case Unit::Count:
        return true;
    }
    return false;
}

template <typename T>
std::size_t checkedBytes(const std::vector<T>& values) {
    if (values.empty() || values.size() > (std::numeric_limits<std::size_t>::max() - kStorageOverhead) / sizeof(T)) {
        throw std::invalid_argument("invalid sample count");
    }
    return values.size() * sizeof(T) + kStorageOverhead;
}
} // namespace

struct MemoryBudget::State {
    explicit State(std::size_t limit_value) : limit(limit_value) {}
    const std::size_t limit;
    std::size_t used = 0;
    std::mutex mutex;
};
struct MemoryBudget::Reservation {
    Reservation(std::shared_ptr<State> owner, std::size_t size) : state(std::move(owner)), bytes(size) {}
    ~Reservation() {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->used -= bytes;
    }
    std::shared_ptr<State> state;
    std::size_t bytes;
};
MemoryBudget::MemoryBudget(std::size_t limit_bytes) : state_(std::make_shared<State>(limit_bytes)) {}
std::size_t MemoryBudget::limit() const { return state_->limit; }
std::size_t MemoryBudget::used() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->used;
}
std::shared_ptr<MemoryBudget::Reservation> MemoryBudget::tryReserve(std::size_t bytes) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (bytes > state_->limit - state_->used) return {};
    auto reservation = std::make_shared<Reservation>(state_, bytes);
    state_->used += bytes;
    return reservation;
}

SampleStorage::SampleStorage(DType dtype, Data data, std::shared_ptr<MemoryBudget::Reservation> reservation)
    : dtype_(dtype), reservation_(std::move(reservation)), data_(std::move(data)) {}
std::shared_ptr<const SampleStorage> SampleStorage::fromInt16(const std::vector<std::int16_t>& values, const MemoryBudget& budget) {
    auto reservation = budget.tryReserve(checkedBytes(values));
    if (!reservation) return {};
    return std::shared_ptr<const SampleStorage>(new SampleStorage(DType::Int16, values, std::move(reservation)));
}
std::shared_ptr<const SampleStorage> SampleStorage::fromInt32(DType dtype, const std::vector<std::int32_t>& values, const MemoryBudget& budget) {
    if (dtype != DType::Int24 && dtype != DType::Int32) throw std::invalid_argument("invalid int32 storage dtype");
    if (dtype == DType::Int24 && std::any_of(values.begin(), values.end(), [](std::int32_t v) {
        return v < -8388608 || v > 8388607;
    })) throw std::invalid_argument("int24 sample out of range");
    auto reservation = budget.tryReserve(checkedBytes(values));
    if (!reservation) return {};
    return std::shared_ptr<const SampleStorage>(new SampleStorage(dtype, values, std::move(reservation)));
}
std::shared_ptr<const SampleStorage> SampleStorage::fromFloat32(const std::vector<float>& values, const MemoryBudget& budget) {
    if (std::any_of(values.begin(), values.end(), [](float v) { return !std::isfinite(v); })) {
        throw std::invalid_argument("non-finite sample");
    }
    auto reservation = budget.tryReserve(checkedBytes(values));
    if (!reservation) return {};
    return std::shared_ptr<const SampleStorage>(new SampleStorage(DType::Float32, values, std::move(reservation)));
}
std::shared_ptr<const SampleStorage> SampleStorage::fromFloat64(const std::vector<double>& values, const MemoryBudget& budget) {
    if (std::any_of(values.begin(), values.end(), [](double v) { return !std::isfinite(v); })) {
        throw std::invalid_argument("non-finite sample");
    }
    auto reservation = budget.tryReserve(checkedBytes(values));
    if (!reservation) return {};
    return std::shared_ptr<const SampleStorage>(new SampleStorage(DType::Float64, values, std::move(reservation)));
}
std::size_t SampleStorage::size() const {
    return std::visit([](const auto& values) { return values.size(); }, data_);
}
std::size_t SampleStorage::bytes() const {
    return std::visit([](const auto& values) { return values.size() * sizeof(typename std::decay_t<decltype(values)>::value_type); }, data_);
}
std::size_t SampleStorage::accountedBytes() const { return reservation_->bytes; }
bool SampleStorage::belongsTo(const MemoryBudget& budget) const { return reservation_->state == budget.state_; }

std::size_t inMemoryBytesPerSample(DType dtype) {
    switch (dtype) {
    case DType::Int16: return sizeof(std::int16_t);
    case DType::Int24:
    case DType::Int32: return sizeof(std::int32_t);
    case DType::Float32: return sizeof(float);
    case DType::Float64: return sizeof(double);
    }
    return 0;
}
double physicalValue(double raw, const ChannelDescriptor& channel) { return raw * channel.scale + channel.offset; }

std::optional<DeviceError> validateStreamDescriptor(const StreamDescriptor& d) {
    if (d.key.session_id.value.empty() || d.key.session_id.value.size() > 64 || d.key.stream_id == 0 ||
        d.capture_id.value.empty() || d.capture_id.value.size() > 64 || d.revision == 0)
        return error("acquisition.invalid_descriptor", "identity");
    if (!d.rate.numerator || !d.rate.denominator) return error("acquisition.invalid_descriptor", "rate");
    if (d.signal_type.empty() || d.signal_type.size() > 4096)
        return error("acquisition.invalid_descriptor", "signal_type");
    if (!inMemoryBytesPerSample(d.dtype)) return error("acquisition.invalid_descriptor", "dtype");
    if (!d.clock.ticks_per_second_num || !d.clock.ticks_per_second_den)
        return error("acquisition.invalid_descriptor", "clock");
    if ((d.clock.source != ClockSource::Hardware && d.clock.source != ClockSource::HostMonotonic &&
         d.clock.source != ClockSource::Estimated) ||
        (d.clock.epoch != ClockEpoch::BootRelative && d.clock.epoch != ClockEpoch::SessionRelative) ||
        (d.sample_index_source != SampleIndexSource::Device && d.sample_index_source != SampleIndexSource::AdapterReceived))
        return error("acquisition.invalid_descriptor", "clock_or_index_source");
    if (!d.max_samples_per_block || d.channels.empty() || d.channels.size() > 4096)
        return error("acquisition.invalid_descriptor", "channels_or_block_limit");
    std::set<std::string> ids;
    for (const auto& channel : d.channels) {
        if (channel.channel_id.empty() || !ids.insert(channel.channel_id).second)
            return error("acquisition.invalid_descriptor", "channel_id");
        if ((channel.unit && !validUnit(*channel.unit)) ||
            !std::isfinite(channel.scale) || channel.scale == 0 || !std::isfinite(channel.offset) ||
            (!channel.unit && (channel.scale != 1.0 || channel.offset != 0.0)))
            return error("acquisition.invalid_descriptor", "calibration");
        if ((channel.minimum && !std::isfinite(*channel.minimum)) ||
            (channel.maximum && !std::isfinite(*channel.maximum)) ||
            (channel.minimum && channel.maximum && *channel.minimum > *channel.maximum))
            return error("acquisition.invalid_descriptor", "range");
        const std::size_t bits = d.dtype == DType::Int24 ? 24 : inMemoryBytesPerSample(d.dtype) * 8;
        if (channel.valid_adc_bits && (*channel.valid_adc_bits == 0 || *channel.valid_adc_bits > bits))
            return error("acquisition.invalid_descriptor", "valid_adc_bits");
    }
    return std::nullopt;
}

std::optional<DeviceError> validateSampleBlock(const StreamDescriptor& d, const SampleBlock& b) {
    if (b.stream != d.key || b.capture_id != d.capture_id || b.generation != d.generation || b.descriptor_revision != d.revision)
        return error("acquisition.stale_block", "identity");
    if (!b.storage || b.storage->dtype() != d.dtype || !b.sample_count ||
        b.sample_count > d.max_samples_per_block || b.channel_count != d.channels.size())
        return error("acquisition.invalid_block", "shape_or_dtype");
    if (b.channel_count > std::numeric_limits<std::size_t>::max() / b.sample_count ||
        b.storage->size() != static_cast<std::size_t>(b.channel_count) * b.sample_count)
        return error("acquisition.invalid_block", "shape");
    if (b.first_sample_index > std::numeric_limits<std::uint64_t>::max() - b.sample_count)
        return error("acquisition.invalid_block", "sample_index");
    if (b.clock_id != d.clock.clock_id || (!b.first_timestamp_ticks && b.timestamp_estimated) ||
        (b.first_timestamp_ticks && d.clock.source == ClockSource::Estimated && !b.timestamp_estimated) ||
        b.host_clock_id.size() > 128 || (b.host_received_monotonic_ns && b.host_clock_id.empty()))
        return error("acquisition.invalid_block", "clock");
    return std::nullopt;
}

std::string newIdentity() {
    static std::mutex mutex;
    static std::mt19937_64 rng([] {
        std::random_device source;
        std::seed_seq seed{source(), source(), source(), source(), source(), source(), source(), source()};
        return std::mt19937_64(seed);
    }());
    std::array<unsigned char, 16> bytes{};
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (std::size_t i = 0; i < bytes.size(); i += 8) {
            auto value = rng();
            for (std::size_t j = 0; j < 8; ++j) { bytes[i + j] = static_cast<unsigned char>(value & 0xff); value >>= 8; }
        }
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) result << '-';
        result << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return result.str();
}
} // namespace bf::acquisition
