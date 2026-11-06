#pragma once

#include "acquisition/model/signal_model.h"
#include <array>

namespace bf::acquisition::rt_bci {

struct Sample {
    std::uint64_t index = 0; // Adapter-received group ordinal; never an expanded device counter.
    std::uint8_t counter = 0;
    bool gap_before = false;
    std::array<std::int32_t, 16> values{};
};
struct Batch {
    std::vector<Sample> samples;
    // Bytes discarded while finding frames or from an incomplete tail.
    // The caller must report an unknown gap when this is nonzero.
    std::size_t discarded_bytes = 0;
    std::optional<DeviceError> error;
    struct Diagnostic {
        std::string reason;
        std::uint64_t byte_offset = 0;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Diagnostic> diagnostics;
};

enum class Layout { Auto };
enum class DecodeState { Hunting, Detecting, Locked, Undetermined };
struct DetectionEvidence {
    std::size_t frames = 0;
    std::size_t pairs = 0;
    std::size_t increments = 0;
};

// Aligned frames preserve BrainFlow's 8-channel and Daisy decoding. Unlike
// that receiver, a malformed byte stream is scanned to regain alignment.
class Decoder {
public:
    // Only 8 and 16 channels are supported; other values throw invalid_argument.
    explicit Decoder(unsigned channels);
    explicit Decoder(Layout layout);
    // At most 64 KiB per call. Process samples before handling a returned error.
    // Only caller-contract/size errors are terminal; wire anomalies are logged.
    Batch feed(const std::uint8_t* data, std::size_t size);
    Batch finish(); // Logs incomplete bytes / orphan bank; complete pairs already published.
    void reset();
    std::optional<unsigned> detectedChannels() const;
    DecodeState state() const;
    DetectionEvidence detectionEvidence() const;
    // Use a user-specified layout after detection failed, replaying buffered frames.
    Batch useFallback(unsigned channels);
    // First accepted output-group counter, not an ADC conversion counter.
    std::optional<std::uint8_t> firstCounter() const;
    // Abandoned first banks; this is not the number of missing device groups.
    std::uint64_t discardedGroups() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
public:
    ~Decoder();
    Decoder(Decoder&&) noexcept;
    Decoder& operator=(Decoder&&) noexcept;
};

} // namespace bf::acquisition::rt_bci
