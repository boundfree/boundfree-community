#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace bf::acquisition {

template <typename Tag>
struct Identity {
    std::string value;
    bool operator==(const Identity& other) const { return value == other.value; }
    bool operator!=(const Identity& other) const { return !(*this == other); }
    bool operator<(const Identity& other) const { return value < other.value; }
};
struct DeviceIdTag;
struct BootIdTag;
struct SessionIdTag;
struct CaptureIdTag;
using DeviceId = Identity<DeviceIdTag>;
using BootId = Identity<BootIdTag>;
using SessionId = Identity<SessionIdTag>;
using CaptureId = Identity<CaptureIdTag>;
using Generation = std::uint64_t;

struct StreamKey {
    SessionId session_id;
    std::uint32_t stream_id = 0;
    bool operator==(const StreamKey& other) const {
        return session_id == other.session_id && stream_id == other.stream_id;
    }
    bool operator!=(const StreamKey& other) const { return !(*this == other); }
    bool operator<(const StreamKey& other) const {
        return session_id < other.session_id ||
               (session_id == other.session_id && stream_id < other.stream_id);
    }
};

enum class DType { Int16, Int24, Int32, Float32, Float64 };
enum class Unit { Volt, Millivolt, Microvolt, Ampere, Milliampere, Pascal, Kilopascal, Celsius, Dimensionless, Count };
enum class ClockSource { Hardware, HostMonotonic, Estimated };
enum class ClockEpoch { BootRelative, SessionRelative };
enum class SampleIndexSource { Device, AdapterReceived };

struct DeviceError {
    std::string code;
    std::map<std::string, std::string> params;
    std::optional<std::uint64_t> operation_id;
    bool retryable = false;
    std::string phase;
};

template <typename T>
struct Result {
    std::optional<T> value;
    std::optional<DeviceError> error;
    explicit operator bool() const { return value.has_value(); }
};

struct Rate {
    std::uint32_t numerator = 0;
    std::uint32_t denominator = 0;
};
struct ClockDescriptor {
    std::uint32_t clock_id = 0;
    std::uint64_t ticks_per_second_num = 0;
    std::uint64_t ticks_per_second_den = 0;
    ClockSource source = ClockSource::Estimated;
    ClockEpoch epoch = ClockEpoch::SessionRelative;
    bool simulated = false; // Synthetic time, never evidence of hardware synchronization.
};
struct ChannelDescriptor {
    std::string channel_id;
    std::string name;
    std::string signal_type;
    std::optional<Unit> unit;
    double scale = 1.0;
    double offset = 0.0;
    std::optional<std::uint8_t> valid_adc_bits;
    std::optional<double> minimum;
    std::optional<double> maximum;
};
struct DeviceDescriptor {
    DeviceId device_id;
    BootId boot_id;
    bool weak_identity = false;
    bool generated_boot_id = false;
    std::string manufacturer;
    std::string model;
    std::string serial_number;
    std::string firmware;
    std::string adapter_version;
    std::vector<std::string> capabilities;
};
struct StreamDescriptor {
    StreamKey key;
    CaptureId capture_id;
    Generation generation = 0;
    std::uint32_t revision = 1;
    std::string name;
    Rate rate;
    DType dtype = DType::Int16;
    ClockDescriptor clock;
    SampleIndexSource sample_index_source = SampleIndexSource::Device;
    std::uint32_t max_samples_per_block = 0;
    std::vector<ChannelDescriptor> channels; // Channel-major storage.
    std::string signal_type; // Stream-level meaning, independent of channel types.
};
struct CaptureDescriptor {
    SessionId session_id;
    CaptureId capture_id;
    Generation generation = 0;
    std::vector<StreamDescriptor> streams;
    std::map<std::string, std::string> metadata;
};

// A shared budget tracks raw samples plus fixed allocation overhead, simulator
// construction buffers and conservative queue reservations across sessions.
class MemoryBudget {
public:
    explicit MemoryBudget(std::size_t limit_bytes);
    std::size_t limit() const;
    std::size_t used() const;
private:
    struct State;
    struct Reservation;
    std::shared_ptr<State> state_;
    std::shared_ptr<Reservation> tryReserve(std::size_t bytes) const;
    friend class SampleStorage;
    friend class SignalConsumer;
    friend class SignalHub;
    friend class InProcessSimulator;
};

// Creation copies the input, so no mutable alias to published storage survives.
// A null result means the budget cannot accommodate the copy; invalid data throws
// std::invalid_argument. Int24 values are sign-extended and stored as int32.
class SampleStorage {
public:
    SampleStorage(const SampleStorage&) = delete;
    SampleStorage& operator=(const SampleStorage&) = delete;
    SampleStorage(SampleStorage&&) = delete;
    SampleStorage& operator=(SampleStorage&&) = delete;
    using Data = std::variant<std::vector<std::int16_t>, std::vector<std::int32_t>,
                              std::vector<float>, std::vector<double>>;
    static std::shared_ptr<const SampleStorage> fromInt16(const std::vector<std::int16_t>&, const MemoryBudget&);
    static std::shared_ptr<const SampleStorage> fromInt32(DType, const std::vector<std::int32_t>&, const MemoryBudget&);
    static std::shared_ptr<const SampleStorage> fromFloat32(const std::vector<float>&, const MemoryBudget&);
    static std::shared_ptr<const SampleStorage> fromFloat64(const std::vector<double>&, const MemoryBudget&);
    DType dtype() const { return dtype_; }
    std::size_t size() const;
    std::size_t bytes() const; // Raw samples, used for per-consumer queue limits.
    std::size_t accountedBytes() const; // Shared budget charge, including overhead.
    bool belongsTo(const MemoryBudget&) const;
    template <typename T> const std::vector<T>* data() const { return std::get_if<std::vector<T>>(&data_); }
private:
    SampleStorage(DType, Data, std::shared_ptr<MemoryBudget::Reservation>);
    DType dtype_;
    std::shared_ptr<MemoryBudget::Reservation> reservation_;
    Data data_; // Destroy storage before releasing its budget reservation.
};

struct SampleBlock {
    StreamKey stream;
    CaptureId capture_id;
    Generation generation = 0;
    std::uint32_t descriptor_revision = 0;
    std::uint32_t channel_count = 0;
    std::uint32_t sample_count = 0;
    std::uint64_t first_sample_index = 0;
    std::optional<std::uint64_t> first_timestamp_ticks;
    std::uint32_t clock_id = 0;
    bool timestamp_estimated = false;
    std::optional<std::uint64_t> host_received_monotonic_ns;
    std::string host_clock_id;
    std::uint32_t quality_flags = 0;
    std::shared_ptr<const SampleStorage> storage;
};

// A null count means the amount of missing data is unknown; no zero samples are inserted.
struct SignalGap {
    StreamKey stream;
    std::optional<std::uint64_t> first_sample_index;
    std::optional<std::uint64_t> count;
    std::string reason;
};
struct SignalEvent {
    std::string event_id;
    StreamKey stream;
    CaptureId capture_id;
    Generation generation = 0;
    std::string kind;
    std::optional<std::uint64_t> sample_index;
    std::optional<std::uint64_t> timestamp_ticks;
    std::optional<SignalGap> gap;
    std::map<std::string, std::string> params;
};
struct ClockMapping {
    std::uint32_t clock_id = 0;
    std::uint64_t start_ticks = 0;
    std::optional<std::uint64_t> end_ticks; // Exclusive when known.
    double offset_seconds = 0.0; // Session time at start_ticks.
    double seconds_per_tick = 0.0;
    std::optional<double> error_bound_seconds;
};

// These functions validate the in-process contract, not a BFSP wire message.
std::optional<DeviceError> validateStreamDescriptor(const StreamDescriptor&);
std::optional<DeviceError> validateSampleBlock(const StreamDescriptor&, const SampleBlock&);
std::size_t inMemoryBytesPerSample(DType);
double physicalValue(double raw, const ChannelDescriptor&);
std::string newIdentity(); // RFC 4122-shaped random identifier; used for session/capture IDs.

} // namespace bf::acquisition
