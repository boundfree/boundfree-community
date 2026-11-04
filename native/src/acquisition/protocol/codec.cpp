#include "internal.h"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace bf::bfsp {
namespace {

constexpr std::array<std::uint32_t, 256> make_crc_table()
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        auto value = i;
        for (int bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1) ? 0x82f63b78u : 0u);
        table[i] = value;
    }
    return table;
}

constexpr auto crc_table = make_crc_table();
static_assert(sizeof(float) == 4 && sizeof(double) == 8 &&
              std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559,
              "BFSP requires IEEE-754 binary32 and binary64");

Result validate_payload(const Header& header, QByteArrayView payload, Limits limits)
{
    if (header.type == MessageType::Data) {
        DataPrefix prefix;
        return decode_data(payload, prefix, limits);
    }
    return validate_json(header.type, header.stream_id, payload, nullptr, limits);
}
}  // namespace

const char* error_code(Error error) noexcept
{
    switch (error) {
    case Error::None: return "";
    case Error::VersionMismatch: return "bfsp.version_mismatch";
    case Error::LimitExceeded: return "bfsp.limit_exceeded";
    case Error::InvalidPayload: return "bfsp.invalid_payload";
    case Error::Timeout: return "bfsp.timeout";
    case Error::Faulted: return "bfsp.faulted";
    default: return "bfsp.invalid_frame";
    }
}

std::uint32_t crc32c(QByteArrayView bytes) noexcept
{
    std::uint32_t crc = 0xffffffffu;
    for (char byte : bytes)
        crc = crc_table[(crc ^ static_cast<unsigned char>(byte)) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

int detail::sample_width(DType type) noexcept
{
    switch (type) {
    case DType::Int16: return 2;
    case DType::Int24: return 3;
    case DType::Int32: case DType::Float32: return 4;
    case DType::Float64: return 8;
    }
    return 0;
}

Result detail::parse_header(QByteArrayView bytes, Header& output, Limits limits)
{
    limits = bounded(limits);
    if (bytes.size() < kHeaderBytes) return fail(Error::Truncated, "header");
    if (bytes.first(4) != QByteArrayView("BFSP", 4)) return fail(Error::InvalidMagic, "magic");
    if (static_cast<unsigned char>(bytes[4]) != 1) return fail(Error::VersionMismatch, "major");
    const auto type = static_cast<unsigned char>(bytes[5]);
    if (type < 1 || type > 10) return fail(Error::InvalidHeader, "message_type");
    if (read_le(bytes, 6, 2) != 0 || read_le(bytes, 8, 2) != kHeaderBytes ||
        read_le(bytes, 10, 2) != 0) return fail(Error::InvalidHeader, "reserved_or_header_length");
    Header header;
    header.type = static_cast<MessageType>(type);
    header.payload_length = static_cast<std::uint32_t>(read_le(bytes, 12, 4));
    header.stream_id = static_cast<std::uint32_t>(read_le(bytes, 16, 4));
    header.sequence = static_cast<std::uint32_t>(read_le(bytes, 20, 4));
    if (header.payload_length > limits.max_payload) return fail(Error::LimitExceeded, "payload_length");
    if (header.type != MessageType::Data) {
        const auto max_json = (header.type == MessageType::Hello || header.type == MessageType::HelloAck)
                ? qMin(limits.max_json, std::uint32_t{4096}) : limits.max_json;
        if (header.payload_length > max_json) return fail(Error::LimitExceeded, "json_length");
    }
    const bool nonzero = header.type == MessageType::Descriptor || header.type == MessageType::Data ||
                         header.type == MessageType::StreamEnd;
    const bool zero = header.type == MessageType::Hello || header.type == MessageType::HelloAck ||
                      header.type == MessageType::Command || header.type == MessageType::Response ||
                      header.type == MessageType::Status || header.type == MessageType::Heartbeat;
    if ((nonzero && header.stream_id == 0) || (zero && header.stream_id != 0))
        return fail(Error::InvalidHeader, "stream_id");
    output = header;
    return {};
}

Result decode_data(QByteArrayView payload, DataPrefix& output, Limits limits)
{
    limits = detail::bounded(limits);
    if (payload.size() > limits.max_payload) return detail::fail(Error::LimitExceeded, "data_length");
    if (payload.size() < kDataPrefixBytes) return detail::fail(Error::InvalidPayload, "data_prefix");
    DataPrefix p;
    p.descriptor_revision = static_cast<std::uint32_t>(detail::read_le(payload, 0, 4));
    p.dtype = static_cast<DType>(static_cast<unsigned char>(payload[4]));
    const auto layout = static_cast<unsigned char>(payload[5]);
    p.time_flags = static_cast<std::uint16_t>(detail::read_le(payload, 6, 2));
    p.channel_count = static_cast<std::uint32_t>(detail::read_le(payload, 8, 4));
    p.sample_count = static_cast<std::uint32_t>(detail::read_le(payload, 12, 4));
    p.first_sample_index = detail::read_le(payload, 16, 8);
    p.first_timestamp_ticks = detail::read_le(payload, 24, 8);
    p.clock_id = static_cast<std::uint32_t>(detail::read_le(payload, 32, 4));
    p.quality_flags = static_cast<std::uint32_t>(detail::read_le(payload, 36, 4));
    const int width = detail::sample_width(p.dtype);
    if (!p.descriptor_revision || !width || layout || (p.time_flags & ~3u) ||
        (p.quality_flags & ~7u) || detail::read_le(payload, 40, 8) ||
        ((p.time_flags & 2u) && !(p.time_flags & 1u)) ||
        (!(p.time_flags & 1u) && p.first_timestamp_ticks))
        return detail::fail(Error::InvalidPayload, "data_fields");
    if (!p.channel_count || p.channel_count > limits.max_channels || !p.sample_count)
        return detail::fail(Error::InvalidPayload, "data_shape");
    if (p.first_sample_index > std::numeric_limits<std::uint64_t>::max() - p.sample_count)
        return detail::fail(Error::InvalidPayload, "sample_index_overflow");
    // Both operands are u32; their product fits u64. Divide before multiplying by width.
    const std::uint64_t count = std::uint64_t(p.channel_count) * p.sample_count;
    const std::uint64_t available = static_cast<std::uint64_t>(payload.size() - kDataPrefixBytes);
    if (count > available / static_cast<unsigned>(width) || count * width != available)
        return detail::fail(Error::InvalidPayload, "data_shape");
    if (p.dtype == DType::Float32 || p.dtype == DType::Float64) {
        for (std::uint64_t i = 0; i < count; ++i) {
            double value = 0;
            const auto result = sample_as_double(payload, p, i, value);
            if (!result) return result;
        }
    }
    output = p;
    return {};
}

Result sample_as_double(QByteArrayView payload, const DataPrefix& p, std::uint64_t index, double& output)
{
    const int width = detail::sample_width(p.dtype);
    const std::uint64_t count = std::uint64_t(p.channel_count) * p.sample_count;
    if (!width || index >= count || payload.size() < kDataPrefixBytes ||
        index > (static_cast<std::uint64_t>(payload.size() - kDataPrefixBytes) / width) ||
        (index + 1) > (static_cast<std::uint64_t>(payload.size() - kDataPrefixBytes) / width))
        return detail::fail(Error::InvalidPayload, "sample_index");
    const auto offset = kDataPrefixBytes + static_cast<qsizetype>(index * width);
    const auto raw = detail::read_le(payload, offset, width);
    double value = 0;
    if (p.dtype == DType::Float32) {
        const auto bits = static_cast<std::uint32_t>(raw);
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        value = f;
    } else if (p.dtype == DType::Float64) {
        std::memcpy(&value, &raw, sizeof(value));
    } else {
        const auto sign = std::uint64_t{1} << (width * 8 - 1);
        value = raw & sign ? static_cast<double>(static_cast<std::int64_t>(raw) -
                                                static_cast<std::int64_t>(sign * 2))
                           : static_cast<double>(raw);
    }
    if (!std::isfinite(value)) return detail::fail(Error::InvalidPayload, "nonfinite_sample");
    output = value;
    return {};
}

Result decode_frame(QByteArrayView bytes, Frame& output, Limits limits)
{
    Header header;
    auto result = detail::parse_header(bytes, header, limits);
    if (!result) return result;
    const auto expected = qsizetype(kHeaderBytes) + header.payload_length + kTrailerBytes;
    if (bytes.size() != expected) return detail::fail(Error::Truncated, "frame_length");
    const auto body = bytes.first(expected - kTrailerBytes);
    if (crc32c(body) != detail::read_le(bytes, expected - kTrailerBytes, kTrailerBytes))
        return detail::fail(Error::InvalidCrc, "crc32c");
    const auto payload = bytes.sliced(kHeaderBytes, header.payload_length);
    result = validate_payload(header, payload, limits);
    if (!result) return result;
    Frame frame{header, QByteArray(payload.data(), payload.size())};
    output = std::move(frame);
    return {};
}

Result encode_frame(MessageType type, std::uint32_t stream_id, std::uint32_t sequence,
                    QByteArrayView payload, QByteArray& output, Limits limits)
{
    limits = detail::bounded(limits);
    if (payload.size() > limits.max_payload) return detail::fail(Error::LimitExceeded, "payload_length");
    QByteArray bytes;
    bytes.reserve(kHeaderBytes + payload.size() + kTrailerBytes);
    bytes.append("BFSP", 4);
    bytes.append(char(1));
    bytes.append(static_cast<char>(type));
    detail::append_le(bytes, 0, 2);
    detail::append_le(bytes, kHeaderBytes, 2);
    detail::append_le(bytes, 0, 2);
    detail::append_le(bytes, payload.size(), 4);
    detail::append_le(bytes, stream_id, 4);
    detail::append_le(bytes, sequence, 4);
    Header header;
    auto result = detail::parse_header(QByteArrayView(bytes), header, limits);
    if (!result) return result;
    result = validate_payload(header, payload, limits);
    if (!result) return result;
    bytes.append(payload.data(), payload.size());
    detail::append_le(bytes, crc32c(QByteArrayView(bytes)), kTrailerBytes);
    output = std::move(bytes);
    return {};
}

}  // namespace bf::bfsp
