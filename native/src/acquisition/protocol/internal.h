#pragma once

#include "codec.h"

#include <limits>

namespace bf::bfsp::detail {

inline std::uint64_t read_le(QByteArrayView bytes, qsizetype offset, int width)
{
    std::uint64_t value = 0;
    for (int i = 0; i < width; ++i)
        value |= std::uint64_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    return value;
}

inline void append_le(QByteArray& bytes, std::uint64_t value, int width)
{
    for (int i = 0; i < width; ++i)
        bytes.append(static_cast<char>((value >> (8 * i)) & 0xff));
}

inline Result fail(Error error, const char* detail)
{
    return {error, QString::fromLatin1(detail)};
}

inline Limits bounded(Limits limits)
{
    limits.max_payload = qMin(limits.max_payload, kMaxPayload);
    limits.max_json = qMin(limits.max_json, kMaxJson);
    limits.max_channels = qMin(limits.max_channels, kMaxChannels);
    return limits;
}

int sample_width(DType type) noexcept;
Result parse_header(QByteArrayView bytes, Header& output, Limits limits);
Result parse_json(QByteArrayView bytes, QJsonObject& output);

}  // namespace bf::bfsp::detail
