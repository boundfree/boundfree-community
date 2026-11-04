#include "acquisition/buffer/signal_hub.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace bf::acquisition {
namespace {
DeviceError hubError(const char* code) { return {code, {}, std::nullopt, false, "buffer"}; }
void addSaturated(std::uint64_t& target, std::uint64_t value) {
    target = value > std::numeric_limits<std::uint64_t>::max() - target
        ? std::numeric_limits<std::uint64_t>::max() : target + value;
}
void appendKnown(std::optional<SignalGap>& pending, const SignalGap& gap) {
    if (pending && pending->first_sample_index && pending->count && gap.first_sample_index && gap.count &&
        *pending->first_sample_index + *pending->count == *gap.first_sample_index) {
        *pending->count += *gap.count;
    } else {
        pending = gap;
    }
}
}

SignalConsumer::SignalConsumer(ConsumerKind kind, ConsumerLimits limits, std::shared_ptr<MemoryBudget::Reservation> reservation)
    : kind_(kind), limits_(limits), metadata_reservation_(std::move(reservation)) {}
std::optional<Delivery> SignalConsumer::pop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return std::nullopt;
    Delivery result = std::move(queue_.front());
    queue_.pop_front();
    stats_.queued_bytes -= result.block->storage->bytes();
    stats_.queued_blocks = queue_.size();
    return result;
}
std::vector<Delivery> SignalConsumer::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {queue_.begin(), queue_.end()};
}
std::vector<SignalGap> SignalConsumer::pendingGaps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SignalGap> result;
    for (const auto& entry : pending_gaps_) {
        if (entry.second.known) result.push_back(*entry.second.known);
        if (entry.second.unknown) result.push_back(*entry.second.unknown);
    }
    return result;
}
ConsumerStats SignalConsumer::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}
void SignalConsumer::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.state == ConsumerState::Active) stats_.state = ConsumerState::Stopped;
}
void SignalConsumer::noteGap(const SignalGap& gap) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stats_.state != ConsumerState::Active) return;
    auto& pending = pending_gaps_[gap.stream];
    if (gap.count) {
        addSaturated(stats_.known_missing_samples, *gap.count);
        appendKnown(pending.known, gap);
    } else {
        addSaturated(stats_.unknown_gaps, 1);
        pending.unknown = gap;
    }
}
void SignalConsumer::offer(const Delivery& delivery) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& block = *delivery.block;
    const auto bytes = block.storage->bytes();
    if (stats_.state == ConsumerState::Stopped) return;
    if (stats_.state == ConsumerState::Failed) {
        addSaturated(stats_.rejected_samples, block.sample_count);
        return;
    }
    const auto fits = [&] {
        return bytes <= limits_.max_bytes && stats_.queued_bytes <= limits_.max_bytes - bytes &&
               queue_.size() < limits_.max_blocks;
    };
    if (!fits() && kind_ == ConsumerKind::Recording) {
        stats_.state = ConsumerState::Failed;
        addSaturated(stats_.rejected_samples, block.sample_count);
        stats_.first_rejected = SignalGap{block.stream, block.first_sample_index, block.sample_count, "recorder.queue_full"};
        return;
    }
    while (!fits() && !queue_.empty()) {
        const auto& old = queue_.front().block;
        addSaturated(stats_.skipped_samples, old->sample_count);
        stats_.queued_bytes -= old->storage->bytes();
        queue_.pop_front();
    }
    if (!fits()) {
        addSaturated(stats_.skipped_samples, block.sample_count);
        stats_.queued_blocks = queue_.size();
        return;
    }
    Delivery scoped = delivery;
    const auto pending = pending_gaps_.find(block.stream);
    if (pending != pending_gaps_.end()) {
        if (pending->second.known) scoped.gaps_before.push_back(*pending->second.known);
        if (pending->second.unknown) scoped.gaps_before.push_back(*pending->second.unknown);
    }
    queue_.push_back(std::move(scoped));
    if (pending != pending_gaps_.end()) pending_gaps_.erase(pending);
    stats_.queued_bytes += bytes;
    stats_.peak_queued_bytes = std::max(stats_.peak_queued_bytes, stats_.queued_bytes);
    stats_.queued_blocks = queue_.size();
    addSaturated(stats_.accepted_samples, block.sample_count);
    stats_.last_accepted_exclusive[block.stream] = block.first_sample_index + block.sample_count;
}

SignalHub::SignalHub(CaptureDescriptor capture, const MemoryBudget& budget)
    : capture_(std::move(capture)), budget_(budget) {
    if (capture_.session_id.value.empty() || capture_.capture_id.value.empty() ||
        capture_.streams.empty() || capture_.streams.size() > 64) throw std::invalid_argument("invalid capture");
    std::map<std::uint32_t, ClockDescriptor> clocks;
    for (const auto& descriptor : capture_.streams) {
        if (validateStreamDescriptor(descriptor) || descriptor.key.session_id != capture_.session_id ||
            descriptor.capture_id != capture_.capture_id || descriptor.generation != capture_.generation ||
            !streams_.emplace(descriptor.key, StreamState{descriptor, 0, std::nullopt, std::nullopt, std::nullopt, false}).second)
            throw std::invalid_argument("invalid capture stream");
        auto insertion = clocks.emplace(descriptor.clock.clock_id, descriptor.clock);
        const auto& clock = insertion.first->second;
        if (!insertion.second && (clock.ticks_per_second_num != descriptor.clock.ticks_per_second_num ||
            clock.ticks_per_second_den != descriptor.clock.ticks_per_second_den ||
            clock.source != descriptor.clock.source || clock.epoch != descriptor.clock.epoch ||
            clock.simulated != descriptor.clock.simulated)) throw std::invalid_argument("conflicting clock domain");
    }
}
Result<std::shared_ptr<SignalConsumer>> SignalHub::addConsumer(ConsumerKind kind, ConsumerLimits limits) {
    std::lock_guard<std::mutex> lock(mutex_);
    pruneConsumers();
    if (closed_ || !limits.max_bytes || limits.max_bytes > budget_.limit() || !limits.max_blocks ||
        limits.max_blocks > 65536 || consumers_.size() >= 32)
        return {std::nullopt, hubError("acquisition.consumer_limit")};
    // Account conservatively for bounded queue nodes, delivery metadata and
    // per-stream pending gaps. Sample storage is accounted once, separately.
    const std::size_t entry_bytes = sizeof(Delivery) + sizeof(SampleBlock) + 2 * sizeof(SignalGap) + 896;
    const std::size_t stream_bytes = 2 * sizeof(SignalGap) + 1152;
    const std::size_t fixed = 2048 + streams_.size() * stream_bytes;
    if (limits.max_blocks > (std::numeric_limits<std::size_t>::max() - fixed) / entry_bytes)
        return {std::nullopt, hubError("acquisition.consumer_limit")};
    auto reservation = budget_.tryReserve(fixed + limits.max_blocks * entry_bytes);
    if (!reservation) return {std::nullopt, hubError("buffer.budget_exhausted")};
    auto consumer = std::shared_ptr<SignalConsumer>(new SignalConsumer(kind, limits, std::move(reservation)));
    for (const auto& entry : streams_) consumer->stats_.start_sample_index.emplace(entry.first, entry.second.next_index);
    consumers_.push_back(consumer);
    return {consumer, std::nullopt};
}
void SignalHub::removeConsumer(const std::shared_ptr<SignalConsumer>& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    pruneConsumers();
    const auto it = std::find_if(consumers_.begin(), consumers_.end(), [&](const auto& item) { return item.lock() == consumer; });
    if (it != consumers_.end()) {
        consumer->stop();
        consumers_.erase(it);
    }
}
void SignalHub::pruneConsumers() {
    consumers_.erase(std::remove_if(consumers_.begin(), consumers_.end(), [](const auto& item) {
        return item.expired();
    }), consumers_.end());
}
PublishResult SignalHub::publish(const SampleBlock& block) {
    std::lock_guard<std::mutex> lock(mutex_);
    pruneConsumers();
    auto it = streams_.find(block.stream);
    if (closed_ || it == streams_.end()) return {PublishStatus::Stale, std::nullopt, hubError("acquisition.stale_block")};
    auto& state = it->second;
    if (auto invalid = validateSampleBlock(state.descriptor, block)) {
        const bool stale = invalid->code == "acquisition.stale_block";
        return {stale ? PublishStatus::Stale : PublishStatus::Invalid, std::nullopt, *invalid};
    }
    if (!block.storage->belongsTo(budget_)) return {PublishStatus::Invalid, std::nullopt, hubError("acquisition.budget_mismatch")};
    const auto end = block.first_sample_index + block.sample_count;
    if (end <= state.next_index) {
        addSaturated(stats_.duplicate_or_late_blocks, 1);
        return {PublishStatus::DuplicateOrLate, std::nullopt, std::nullopt};
    }
    if (block.first_sample_index < state.next_index) {
        addSaturated(stats_.overlap_blocks, 1);
        return {PublishStatus::Overlap, std::nullopt, hubError("acquisition.overlap")};
    }
    if (state.clock_fault || (block.first_timestamp_ticks && state.last_timestamp &&
                             *block.first_timestamp_ticks < *state.last_timestamp)) {
        state.clock_fault = true;
        return {PublishStatus::ClockDiscontinuity, std::nullopt, hubError("clock.discontinuity")};
    }
    std::optional<SignalGap> gap;
    if (block.first_sample_index > state.next_index) {
        gap = SignalGap{block.stream, state.next_index, block.first_sample_index - state.next_index, "sample_index.gap"};
        appendKnown(state.pending_known_gap, *gap);
        addSaturated(stats_.known_missing_samples, *gap->count);
        for (const auto& subscriber : consumers_) if (auto consumer = subscriber.lock()) consumer->noteGap(*gap);
    }
    Delivery delivery;
    delivery.block = std::make_shared<const SampleBlock>(block);
    state.pending_known_gap.reset();
    state.pending_unknown_gap.reset();
    state.next_index = end;
    if (block.first_timestamp_ticks) state.last_timestamp = block.first_timestamp_ticks;
    addSaturated(stats_.accepted_samples, block.sample_count);
    for (const auto& subscriber : consumers_) if (auto consumer = subscriber.lock()) consumer->offer(delivery);
    return {PublishStatus::Accepted, gap, std::nullopt};
}
std::optional<DeviceError> SignalHub::reportGap(const SignalGap& gap) {
    std::lock_guard<std::mutex> lock(mutex_);
    pruneConsumers();
    auto it = streams_.find(gap.stream);
    if (closed_ || it == streams_.end()) return hubError("acquisition.stale_stream");
    if (gap.reason.empty() || gap.reason.size() > 256) return hubError("acquisition.invalid_gap");
    auto& state = it->second;
    if (gap.count) {
        if (!gap.first_sample_index || !*gap.count || *gap.first_sample_index != state.next_index ||
            *gap.count > std::numeric_limits<std::uint64_t>::max() - state.next_index)
            return hubError("acquisition.invalid_gap");
        appendKnown(state.pending_known_gap, gap);
        state.next_index += *gap.count;
        addSaturated(stats_.known_missing_samples, *gap.count);
    } else {
        if (gap.first_sample_index && *gap.first_sample_index != state.next_index)
            return hubError("acquisition.invalid_gap");
        state.pending_unknown_gap = gap;
        addSaturated(stats_.unknown_gaps, 1);
    }
    for (const auto& subscriber : consumers_) if (auto consumer = subscriber.lock()) consumer->noteGap(gap);
    return std::nullopt;
}
std::optional<DeviceError> SignalHub::reportUnknownGap(const StreamKey& stream, std::string reason) {
    return reportGap(SignalGap{stream, std::nullopt, std::nullopt, std::move(reason)});
}
std::vector<SignalGap> SignalHub::pendingGaps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SignalGap> result;
    for (const auto& entry : streams_) {
        if (entry.second.pending_known_gap) result.push_back(*entry.second.pending_known_gap);
        if (entry.second.pending_unknown_gap) result.push_back(*entry.second.pending_unknown_gap);
    }
    return result;
}
void SignalHub::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) return;
    closed_ = true;
    pruneConsumers();
    for (const auto& subscriber : consumers_) if (auto consumer = subscriber.lock()) consumer->stop();
    consumers_.clear();
}
HubStats SignalHub::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}
} // namespace bf::acquisition
