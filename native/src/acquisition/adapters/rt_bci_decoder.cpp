#include "acquisition/adapters/rt_bci_decoder.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace bf::acquisition::rt_bci {
namespace {
constexpr std::size_t frame_size = 33;
constexpr std::size_t max_input_size = 64 * 1024;
std::int32_t int24(const std::uint8_t* p) {
    const auto n = (std::uint32_t(p[0]) << 16) | (std::uint32_t(p[1]) << 8) | p[2];
    return n & 0x800000U ? static_cast<std::int32_t>(n) - 0x1000000 : static_cast<std::int32_t>(n);
}
}
struct Decoder::Impl {
    explicit Impl(unsigned n) : channels(n) {}
    unsigned channels;
    std::array<std::uint8_t, frame_size> frame{};
    std::size_t used = 0;
    std::uint64_t bytes_seen = 0, next_index = 0, discarded_groups = 0;
    bool finished = false, gap = false, first_bank = true, pending_published = false;
    std::optional<DeviceError> failure;
    std::optional<std::uint8_t> first_counter, last_counter;
    Sample pending{};

    void note(Batch& batch, const char* reason, std::size_t count) {
        batch.diagnostics.push_back({reason, bytes_seen - count,
            std::vector<std::uint8_t>(frame.begin(), frame.begin() + count)});
    }
    void publish(Batch& batch, Sample sample) {
        if (next_index == std::numeric_limits<std::uint64_t>::max()) {
            failure = DeviceError{"acquisition.counter_exhausted", {{"reason", "counter_exhausted"}}, std::nullopt, false, "rt-bci.decode"};
            batch.error = failure;
            return;
        }
        sample.index = next_index++;
        sample.gap_before = std::exchange(gap, false);
        if (!first_counter) first_counter = sample.counter;
        batch.samples.push_back(std::move(sample));
    }
    void accept(Batch& batch) {
        if (frame[0] != 0xa0 || frame[32] < 0xc0 || frame[32] > 0xc6) {
            note(batch, frame[0] != 0xa0 ? "unexpected_frame_start" : "invalid_frame_end", frame_size);
            batch.discarded_bytes += frame_size;
            gap = true;
            return; // Same fixed-size packet skip as the reference receiver.
        }
        const auto counter = frame[1];
        if (channels == 8) {
            if (last_counter && counter != static_cast<std::uint8_t>(*last_counter + 1)) {
                note(batch, counter == *last_counter ? "duplicate_counter" : "counter_discontinuity", frame_size);
                // A repeated wire ID can carry distinct valid samples (or a
                // second bank). It does not prove that any data was lost.
                // Turning every duplicate into a gap erases all line segments
                // after min/max display reduction. Keep the diagnostic, and
                // leave any prior real framing gap intact.
                if (counter != *last_counter) gap = true;
            }
            Sample sample; sample.counter = counter;
            for (unsigned i = 0; i < 8; ++i) sample.values[i] = int24(frame.data() + 2 + 3 * i);
            last_counter = counter;
            publish(batch, sample);
            return;
        }
        // Reference Daisy behavior: an ID change selects the first bank; each
        // further packet with the same ID replaces bank two and publishes.
        // This also preserves the reference behavior for three equal IDs.
        if (!last_counter || counter != *last_counter) {
            if (!first_bank && !pending_published) { ++discarded_groups; note(batch, "unpaired_frame", frame_size); gap = true; }
            if (last_counter && counter != static_cast<std::uint8_t>(*last_counter + 1)) {
                note(batch, "counter_discontinuity", frame_size); gap = true;
            }
            first_bank = true;
        }
        last_counter = counter;
        if (first_bank) {
            pending = {}; pending.counter = counter;
            pending_published = false;
            for (unsigned i = 0; i < 8; ++i) pending.values[i] = int24(frame.data() + 2 + 3 * i);
        } else {
            if (pending_published) {
                note(batch, "repeated_second_bank", frame_size);
                gap = true;
            }
            for (unsigned i = 0; i < 8; ++i) pending.values[i + 8] = int24(frame.data() + 2 + 3 * i);
            publish(batch, pending);
            pending_published = true;
        }
        first_bank = false;
    }
};
Decoder::Decoder(unsigned channels) {
    if (channels != 8 && channels != 16) throw std::invalid_argument("RT-BCI supports 8 or 16 channels");
    d = std::make_unique<Impl>(channels);
}
Decoder::~Decoder() = default;
Decoder::Decoder(Decoder&&) noexcept = default;
Decoder& Decoder::operator=(Decoder&&) noexcept = default;
Batch Decoder::feed(const std::uint8_t* data, std::size_t size) {
    Batch result;
    if (d->failure || d->finished || size > max_input_size || (!data && size)) {
        if (!d->failure) d->failure = DeviceError{
            size > max_input_size ? "protocol.limit_exceeded" : "protocol.invalid_payload",
            {{"reason", d->finished ? "decoder_finished" : size > max_input_size ? "input_too_large" : "null_input"}},
            std::nullopt, false, "rt-bci.decode"};
        result.error = d->failure;
        result.discarded_bytes = size;
        return result;
    }
    for (std::size_t i = 0; i < size; ++i) {
        d->frame[d->used++] = data[i]; ++d->bytes_seen;
        if (d->used != frame_size) continue;
        d->accept(result); d->used = 0;
        if (result.error) break;
    }
    return result;
}
Batch Decoder::finish() {
    Batch result;
    if (d->failure) { result.error = d->failure; return result; }
    if (d->finished) return result;
    if (d->used) {
        d->note(result, "truncated_frame", d->used);
        result.discarded_bytes = d->used; d->used = 0;
    }
    // An odd first bank never became a complete published group.
    if (d->channels == 16 && !d->first_bank && !d->pending_published) {
        ++d->discarded_groups;
        result.diagnostics.push_back({"unpaired_tail", d->bytes_seen, {}});
    }
    d->finished = true;
    return result;
}
void Decoder::reset() { d = std::make_unique<Impl>(d->channels); }
std::optional<std::uint8_t> Decoder::firstCounter() const { return d->first_counter; }
std::uint64_t Decoder::discardedGroups() const { return d->discarded_groups; }
} // namespace bf::acquisition::rt_bci
