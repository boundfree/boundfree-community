#include "acquisition/adapters/rt_bci_decoder.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace bf::acquisition::rt_bci {
namespace {
constexpr std::size_t frame_size = 33;
constexpr std::size_t max_input_size = 64 * 1024;
constexpr std::size_t min_detection_frames = 24;
constexpr std::size_t max_detection_frames = 200;
constexpr std::size_t max_hunt_bytes = frame_size * max_detection_frames;
using Frame = std::array<std::uint8_t, frame_size>;

bool validFrame(const std::uint8_t* p) {
    return p[0] == 0xa0 && p[32] >= 0xc0 && p[32] <= 0xc6;
}
std::int32_t int24(const std::uint8_t* p) {
    const auto n = (std::uint32_t(p[0]) << 16) | (std::uint32_t(p[1]) << 8) | p[2];
    return n & 0x800000U ? static_cast<std::int32_t>(n) - 0x1000000 : static_cast<std::int32_t>(n);
}
}
struct Decoder::Impl {
    explicit Impl(unsigned n) : requested(n), channels(n) {}
    struct BufferedFrame { Frame bytes; bool gap_before; std::uint64_t offset; };
    unsigned requested, channels;
    std::vector<std::uint8_t> buffer;
    std::vector<BufferedFrame> detected_frames;
    DetectionEvidence evidence;
    Frame frame{};
    std::uint64_t buffer_offset = 0, frame_offset = 0, next_index = 0, discarded_groups = 0;
    std::size_t hunt_bytes = 0;
    bool synchronized = false, finished = false, gap = false, first_bank = true, pending_published = false;
    std::optional<DeviceError> failure;
    std::optional<std::uint8_t> first_counter, last_counter;
    Sample pending{};
    DecodeState current = DecodeState::Hunting;

    void note(Batch& batch, const char* reason) {
        batch.diagnostics.push_back({reason, frame_offset, {frame.begin(), frame.end()}});
    }
    void discard(Batch& batch, std::size_t count, const char* reason) {
        if (!count) return;
        const auto shown = std::min(count, frame_size);
        batch.diagnostics.push_back({reason, buffer_offset, {buffer.begin(), buffer.begin() + shown}});
        batch.discarded_bytes += count;
        buffer_offset += count;
        buffer.erase(buffer.begin(), buffer.begin() + count);
        gap = true;
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
        const auto counter = frame[1];
        if (channels == 8) {
            if (last_counter && counter != static_cast<std::uint8_t>(*last_counter + 1)) {
                note(batch, counter == *last_counter ? "duplicate_counter" : "counter_discontinuity");
                if (counter != *last_counter) gap = true;
            }
            Sample sample; sample.counter = counter;
            for (unsigned i = 0; i < 8; ++i) sample.values[i] = int24(frame.data() + 2 + 3 * i);
            last_counter = counter;
            publish(batch, sample);
            return;
        }
        // Counter change starts a bank; another frame with the same counter
        // completes it. A leading orphan is abandoned on the next counter.
        if (!last_counter || counter != *last_counter) {
            if (!first_bank && !pending_published) { ++discarded_groups; note(batch, "unpaired_frame"); gap = true; }
            if (last_counter && counter != static_cast<std::uint8_t>(*last_counter + 1)) {
                note(batch, "counter_discontinuity"); gap = true;
            }
            first_bank = true;
        }
        last_counter = counter;
        if (first_bank) {
            pending = {}; pending.counter = counter;
            pending_published = false;
            for (unsigned i = 0; i < 8; ++i) pending.values[i] = int24(frame.data() + 2 + 3 * i);
        } else {
            if (pending_published) { note(batch, "repeated_second_bank"); gap = true; }
            for (unsigned i = 0; i < 8; ++i) pending.values[i + 8] = int24(frame.data() + 2 + 3 * i);
            publish(batch, pending);
            pending_published = true;
        }
        first_bank = false;
    }
    void replay(Batch& batch) {
        for (const auto& saved : detected_frames) {
            frame = saved.bytes;
            frame_offset = saved.offset;
            gap |= saved.gap_before;
            accept(batch);
            if (batch.error) break;
        }
        detected_frames.clear();
    }
    void detect(Batch& batch) {
        detected_frames.push_back({frame, std::exchange(gap, false), frame_offset});
        evidence.frames = detected_frames.size();
        if (evidence.frames > 1) {
            const auto previous = detected_frames[evidence.frames - 2].bytes[1];
            if (frame[1] == previous) ++evidence.pairs;
            else if (frame[1] == static_cast<std::uint8_t>(previous + 1)) ++evidence.increments;
        }
        if (evidence.frames >= min_detection_frames) {
            const auto window_start = evidence.frames - min_detection_frames;
            std::size_t pairs = 0, increments = 0;
            for (std::size_t i = window_start + 1; i < evidence.frames; ++i) {
                const auto previous = detected_frames[i - 1].bytes[1], current = detected_frames[i].bytes[1];
                if (current == previous) ++pairs;
                else if (current == static_cast<std::uint8_t>(previous + 1)) ++increments;
            }
            const auto transitions = min_detection_frames - 1;
            const bool clean = pairs + increments == transitions;
            bool paired = clean && pairs * 10 >= transitions * 4 && increments > 0;
            // Each counter must occur twice, except a possible orphan at either end.
            for (std::size_t start = window_start; paired && start < evidence.frames;) {
                std::size_t end = start + 1;
                while (end < evidence.frames && detected_frames[end].bytes[1] == detected_frames[start].bytes[1]) ++end;
                if (end - start != 2 && !(end - start == 1 && (start == window_start || end == evidence.frames))) paired = false;
                start = end;
            }
            if (clean && pairs == 0) channels = 8;
            else if (paired) channels = 16;
            if (channels) { current = DecodeState::Locked; replay(batch); return; }
        }
        if (evidence.frames == max_detection_frames) current = DecodeState::Undetermined;
    }
    void process(Batch& batch, bool finishing) {
        while (!batch.error && current != DecodeState::Undetermined) {
            if (!synchronized) {
                const std::size_t required = finishing ? frame_size : frame_size * 3;
                if (buffer.size() < required) break;
                std::size_t pos = 0;
                while (pos + required <= buffer.size()) {
                    if (validFrame(buffer.data() + pos) &&
                        (finishing || (validFrame(buffer.data() + pos + frame_size) &&
                                       validFrame(buffer.data() + pos + 2 * frame_size)))) break;
                    ++pos;
                }
                if (pos) { discard(batch, pos, "frame_resync"); hunt_bytes += pos; }
                if (!requested && !channels && hunt_bytes >= max_hunt_bytes) {
                    current = DecodeState::Undetermined; break;
                }
                if (buffer.size() < required) break;
                synchronized = true; hunt_bytes = 0;
                current = channels ? DecodeState::Locked : DecodeState::Detecting;
            }
            if (buffer.size() < frame_size) break;
            if (!validFrame(buffer.data())) {
                synchronized = false; current = DecodeState::Hunting;
                continue;
            }
            std::copy_n(buffer.begin(), frame_size, frame.begin());
            frame_offset = buffer_offset;
            buffer_offset += frame_size;
            buffer.erase(buffer.begin(), buffer.begin() + frame_size);
            if (channels) accept(batch); else detect(batch);
        }
    }
};
Decoder::Decoder(unsigned channels) {
    if (channels != 8 && channels != 16) throw std::invalid_argument("RT-BCI supports 8 or 16 channels");
    d = std::make_unique<Impl>(channels);
}
Decoder::Decoder(Layout) : d(std::make_unique<Impl>(0)) {}
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
    if (size && d->current != DecodeState::Undetermined) d->buffer.insert(d->buffer.end(), data, data + size);
    d->process(result, false);
    return result;
}
Batch Decoder::finish() {
    Batch result;
    if (d->failure) { result.error = d->failure; return result; }
    if (d->finished) return result;
    d->process(result, false);
    d->process(result, true);
    if (!d->buffer.empty()) d->discard(result, d->buffer.size(), "truncated_frame");
    if (!d->channels && d->current != DecodeState::Undetermined) d->current = DecodeState::Undetermined;
    if (d->channels == 16 && !d->first_bank && !d->pending_published) {
        ++d->discarded_groups;
        result.diagnostics.push_back({"unpaired_tail", d->buffer_offset, {}});
    }
    d->finished = true;
    return result;
}
void Decoder::reset() { d = std::make_unique<Impl>(d->requested); }
std::optional<unsigned> Decoder::detectedChannels() const { return d->channels ? std::optional<unsigned>(d->channels) : std::nullopt; }
DecodeState Decoder::state() const { return d->current; }
DetectionEvidence Decoder::detectionEvidence() const { return d->evidence; }
Batch Decoder::useFallback(unsigned channels) {
    if (channels != 8 && channels != 16) throw std::invalid_argument("RT-BCI supports 8 or 16 channels");
    Batch result;
    if (d->channels || d->finished || d->failure) return result;
    d->channels = channels;
    d->current = d->synchronized ? DecodeState::Locked : DecodeState::Hunting;
    d->replay(result);
    d->process(result, false);
    return result;
}
std::optional<std::uint8_t> Decoder::firstCounter() const { return d->first_counter; }
std::uint64_t Decoder::discardedGroups() const { return d->discarded_groups; }
} // namespace bf::acquisition::rt_bci
