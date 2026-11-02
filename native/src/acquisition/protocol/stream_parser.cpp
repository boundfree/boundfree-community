#include "internal.h"

#include <algorithm>
#include <utility>

namespace bf::bfsp {

StreamParser::StreamParser(ParserOptions options) : options_(options)
{
    options_.limits = detail::bounded(options_.limits);
    options_.frame_timeout_ms = std::max<qint64>(1, options_.frame_timeout_ms);
    options_.max_input_per_feed = std::max<qsizetype>(1, options_.max_input_per_feed);
    options_.max_scan_per_feed = std::max<qsizetype>(1, options_.max_scan_per_feed);
    options_.max_frames_per_feed = std::max(1, options_.max_frames_per_feed);
}

void StreamParser::reset()
{
    buffer_.clear();
    arrivals_.clear();
    diagnostics_ = {};
    faulted_ = false;
    have_time_ = false;
    last_now_ms_ = 0;
}

void StreamParser::drop(qsizetype count)
{
    buffer_.remove(0, count);
    while (count > 0) {
        auto& arrival = arrivals_.front();
        const auto removed = std::min(count, arrival.count);
        arrival.count -= removed;
        count -= removed;
        if (!arrival.count) arrivals_.pop_front();
    }
}

void StreamParser::process(qint64 now_ms, qsizetype& scan_budget, int& frame_budget, ParseResult& result)
{
    constexpr char magic[] = "BFSP";
    const auto timed_out = [&](qint64 end_ms) {
        if (end_ms - arrivals_.front().time_ms < options_.frame_timeout_ms) return false;
        drop(1);
        --scan_budget;
        ++diagnostics_.timeouts;
        ++diagnostics_.consecutive_timeouts;
        result.errors.push_back(detail::fail(Error::Timeout, "frame_timeout"));
        if (diagnostics_.consecutive_timeouts >= 3) faulted_ = true;
        return true;
    };
    while (!buffer_.isEmpty() && !faulted_ && scan_budget > 0 && frame_budget > 0) {
        qsizetype skip = 0;
        while (skip < buffer_.size() && skip < scan_budget) {
            qsizetype prefix = 0;
            const auto available = std::min<qsizetype>(buffer_.size() - skip, 4);
            while (prefix < available && buffer_[skip + prefix] == magic[prefix]) ++prefix;
            if (prefix == available) break;
            ++skip;
        }
        if (skip) {
            drop(skip);
            diagnostics_.skipped_bytes += static_cast<std::uint64_t>(skip);
            scan_budget -= skip;
            continue;
        }

        // Time starts at the first byte, including a partial magic split across reads.
        if (buffer_.size() < kHeaderBytes) {
            if (timed_out(now_ms)) continue;
            break;
        }

        Header header;
        auto status = detail::parse_header(QByteArrayView(buffer_), header, options_.limits);
        bool different_major = false;
        if (status.error == Error::VersionMismatch) {
            // A magic-like byte sequence inside a damaged payload is not a
            // confirmed peer version. Check the common framing and CRC first,
            // without assuming its type/flags mean the same as in version 1.
            const auto bytes = QByteArrayView(buffer_);
            const auto length = detail::read_le(bytes, 12, 4);
            if (detail::read_le(bytes, 8, 2) == kHeaderBytes && length <= options_.limits.max_payload) {
                header.payload_length = static_cast<std::uint32_t>(length);
                different_major = true;
            } else {
                status = detail::fail(Error::InvalidHeader, "unknown_major_framing");
            }
        }
        if (!status && !different_major) {
            result.errors.push_back(status);
            drop(1);
            --scan_budget;
            ++diagnostics_.invalid_headers;
            if (++diagnostics_.consecutive_bad >= 8) faulted_ = true;
            continue;
        }
        const auto frame_bytes = qsizetype(kHeaderBytes) + header.payload_length + kTrailerBytes;
        if (buffer_.size() < frame_bytes) {
            if (timed_out(now_ms)) continue;
            break;
        }
        // Buffered complete frames may wait for the work budget. Compare the
        // actual arrival of their last byte, not their later processing time.
        qsizetype remaining = frame_bytes;
        qint64 end_ms = arrivals_.front().time_ms;
        for (const auto& arrival : arrivals_) {
            end_ms = arrival.time_ms;
            remaining -= std::min(remaining, arrival.count);
            if (!remaining) break;
        }
        if (timed_out(end_ms)) {
            continue;
        }
        --frame_budget;
        Frame frame;
        if (different_major) {
            const auto candidate = QByteArrayView(buffer_).first(frame_bytes);
            if (crc32c(candidate.first(frame_bytes - kTrailerBytes)) ==
                detail::read_le(candidate, frame_bytes - kTrailerBytes, kTrailerBytes)) {
                result.errors.push_back(detail::fail(Error::VersionMismatch, "major"));
                faulted_ = true;
                break;
            }
            status = detail::fail(Error::InvalidCrc, "crc32c");
        } else {
            status = decode_frame(QByteArrayView(buffer_).first(frame_bytes), frame, options_.limits);
        }
        if (!status) {
            result.errors.push_back(status);
            if (status.error == Error::InvalidCrc) {
                drop(1);
                --scan_budget;
                ++diagnostics_.invalid_crc;
                if (++diagnostics_.consecutive_bad >= 8) faulted_ = true;
            } else {
                // A verified frame with an invalid payload is not a resync candidate.
                drop(frame_bytes);
                ++diagnostics_.invalid_payloads;
            }
            continue;
        }
        drop(frame_bytes);
        diagnostics_.consecutive_bad = 0;
        diagnostics_.consecutive_timeouts = 0;
        result.frames.push_back(std::move(frame));
    }
}

ParseResult StreamParser::feed(QByteArrayView input, qint64 now_ms)
{
    ParseResult result;
    if (faulted_) {
        result.faulted = true;
        result.errors.push_back(detail::fail(Error::Faulted, "parser_faulted"));
        return result;
    }
    if (now_ms < 0 || (have_time_ && now_ms < last_now_ms_)) {
        // A non-monotonic clock cannot safely enforce total timeouts.
        faulted_ = true;
        result.faulted = true;
        result.errors.push_back(detail::fail(Error::Faulted, "non_monotonic_clock"));
        return result;
    }
    have_time_ = true;
    last_now_ms_ = now_ms;
    auto scan_budget = options_.max_scan_per_feed;
    auto frame_budget = options_.max_frames_per_feed;
    process(now_ms, scan_budget, frame_budget, result);
    const auto capacity = qsizetype(kHeaderBytes) + options_.limits.max_payload + kTrailerBytes;
    const auto available = std::max<qsizetype>(0, capacity - buffer_.size());
    const auto accepted = std::min({input.size(), available, options_.max_input_per_feed});
    if (!faulted_ && accepted > 0) {
        buffer_.append(input.data(), accepted);
        if (!arrivals_.empty() && arrivals_.back().time_ms == now_ms) arrivals_.back().count += accepted;
        else arrivals_.push_back({accepted, now_ms});
        result.consumed = accepted;
        process(now_ms, scan_budget, frame_budget, result);
    }
    result.faulted = faulted_;
    return result;
}

}  // namespace bf::bfsp
