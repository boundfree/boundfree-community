#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <vector>

namespace bf::bfsp {

constexpr std::uint32_t kMaxPayload = 1'048'576;
constexpr std::uint32_t kMaxJson = 65'536;
constexpr std::uint32_t kMaxChannels = 4'096;
constexpr int kHeaderBytes = 24;
constexpr int kDataPrefixBytes = 48;
constexpr int kTrailerBytes = 4;

enum class MessageType : std::uint8_t {
    Hello = 1, HelloAck, Command, Response, Descriptor, Data,
    Event, Status, Heartbeat, StreamEnd
};

enum class Error {
    None, Truncated, InvalidMagic, VersionMismatch, InvalidHeader,
    LimitExceeded, InvalidCrc, InvalidPayload, Timeout, Faulted
};

// Stable, non-localized error code; no diagnostic contains raw sample data.
const char* error_code(Error error) noexcept;

struct Limits {
    std::uint32_t max_payload = kMaxPayload;
    std::uint32_t max_json = kMaxJson;
    std::uint32_t max_channels = kMaxChannels;
};

struct Header {
    MessageType type = MessageType::Heartbeat;
    std::uint32_t payload_length = 0;
    std::uint32_t stream_id = 0;
    std::uint32_t sequence = 0;
};

struct Frame {
    Header header;
    QByteArray payload;
};

enum class DType : std::uint8_t { Int16 = 1, Int24, Int32, Float32, Float64 };

struct DataPrefix {
    std::uint32_t descriptor_revision = 0;
    DType dtype = DType::Int16;
    std::uint16_t time_flags = 0;
    std::uint32_t channel_count = 0;
    std::uint32_t sample_count = 0;
    std::uint64_t first_sample_index = 0;
    std::uint64_t first_timestamp_ticks = 0;
    std::uint32_t clock_id = 0;
    std::uint32_t quality_flags = 0;
};

struct Result {
    Error error = Error::None;
    QString detail;
    explicit operator bool() const noexcept { return error == Error::None; }
};

std::uint32_t crc32c(QByteArrayView bytes) noexcept;

// decode_frame validates the exact complete frame, CRC and payload. On failure,
// output arguments remain unchanged. The session must validate direction,
// sequence, negotiated identity and DATA against its accepted descriptor.
Result decode_frame(QByteArrayView bytes, Frame& output, Limits limits = {});
Result encode_frame(MessageType type, std::uint32_t stream_id,
                    std::uint32_t sequence, QByteArrayView payload,
                    QByteArray& output, Limits limits = {});
Result decode_data(QByteArrayView payload, DataPrefix& output, Limits limits = {});
// Index is channel * sample_count + sample. Integer values are exact in double.
Result sample_as_double(QByteArrayView payload, const DataPrefix& prefix,
                        std::uint64_t index, double& output);
// Checks the standalone wire shape and explicit limits. The session validates
// negotiated limit relationships, features, identities, state and request linkage.
Result validate_json(MessageType type, std::uint32_t stream_id,
                     QByteArrayView payload, QJsonObject* object = nullptr,
                     Limits limits = {});

struct ParserOptions {
    Limits limits;
    qint64 frame_timeout_ms = 5'000;
    qsizetype max_input_per_feed = 65'536;
    qsizetype max_scan_per_feed = 65'536;
    int max_frames_per_feed = 64;  // complete candidates, including invalid CRC/payload
};

struct ParserDiagnostics {
    std::uint64_t invalid_headers = 0;
    std::uint64_t invalid_crc = 0;
    std::uint64_t invalid_payloads = 0;
    std::uint64_t timeouts = 0;
    std::uint64_t skipped_bytes = 0;
    int consecutive_bad = 0;
    int consecutive_timeouts = 0;
};

struct ParseResult {
    // Caller retains and resubmits bytes [consumed, input.size()).
    qsizetype consumed = 0;
    std::vector<Frame> frames;
    std::vector<Result> errors;
    bool faulted = false;
};

// Single-threaded parser. now_ms must be a nondecreasing monotonic clock.
// Call feed({}, now_ms) on a timer and to drain after a work-budget yield.
// A faulted parser must be replaced/reset for a new transport session.
class StreamParser {
public:
    explicit StreamParser(ParserOptions options = {});
    ParseResult feed(QByteArrayView input, qint64 now_ms);
    const ParserDiagnostics& diagnostics() const noexcept { return diagnostics_; }
    qsizetype buffered_bytes() const noexcept { return buffer_.size(); }
    bool faulted() const noexcept { return faulted_; }
    void reset();

private:
    struct Arrival { qsizetype count; qint64 time_ms; };
    void drop(qsizetype count);
    void process(qint64 now_ms, qsizetype& scan_budget, int& frame_budget, ParseResult& result);

    ParserOptions options_;
    QByteArray buffer_;
    std::deque<Arrival> arrivals_;
    ParserDiagnostics diagnostics_;
    bool faulted_ = false;
    qint64 last_now_ms_ = 0;
    bool have_time_ = false;
};

}  // namespace bf::bfsp
