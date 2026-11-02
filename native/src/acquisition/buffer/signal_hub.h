#pragma once

#include "acquisition/model/signal_model.h"

#include <deque>
#include <mutex>

namespace bf::acquisition {

enum class ConsumerKind { History, DspLatest, Recording };
enum class ConsumerState { Active, Stopped, Failed };
struct ConsumerLimits {
    std::size_t max_bytes = 0;
    std::size_t max_blocks = 0;
};
struct Delivery {
    std::shared_ptr<const SampleBlock> block;
    std::vector<SignalGap> gaps_before; // At most one known and one unknown gap.
};
struct ConsumerStats {
    ConsumerState state = ConsumerState::Active;
    std::uint64_t accepted_samples = 0;
    std::uint64_t skipped_samples = 0; // Display/DSP eviction; not acquisition loss.
    std::uint64_t rejected_samples = 0; // Includes blocks after recorder failure.
    std::uint64_t known_missing_samples = 0;
    std::uint64_t unknown_gaps = 0;
    std::size_t queued_bytes = 0;
    std::size_t peak_queued_bytes = 0;
    std::size_t queued_blocks = 0;
    std::optional<SignalGap> first_rejected;
    std::map<StreamKey, std::uint64_t> start_sample_index; // Atomic snapshot when subscribed.
    std::map<StreamKey, std::uint64_t> last_accepted_exclusive;
};

class SignalConsumer {
public:
    std::optional<Delivery> pop();
    std::vector<Delivery> snapshot() const;
    std::vector<SignalGap> pendingGaps() const; // Gaps observed since the last queued block.
    ConsumerStats stats() const;
    void stop(); // Close recording input at an atomic boundary; queued items remain readable.
private:
    friend class SignalHub;
    SignalConsumer(ConsumerKind, ConsumerLimits, std::shared_ptr<MemoryBudget::Reservation>);
    void offer(const Delivery&);
    void noteGap(const SignalGap&);
    ConsumerKind kind_;
    ConsumerLimits limits_;
    mutable std::mutex mutex_;
    std::shared_ptr<MemoryBudget::Reservation> metadata_reservation_;
    std::deque<Delivery> queue_;
    struct PendingGaps {
        std::optional<SignalGap> known;
        std::optional<SignalGap> unknown;
    };
    std::map<StreamKey, PendingGaps> pending_gaps_;
    ConsumerStats stats_;
};

enum class PublishStatus { Accepted, DuplicateOrLate, Overlap, Stale, Invalid, ClockDiscontinuity };
struct PublishResult {
    PublishStatus status = PublishStatus::Invalid;
    std::optional<SignalGap> gap;
    std::optional<DeviceError> error;
};
struct HubStats {
    std::uint64_t accepted_samples = 0;
    std::uint64_t known_missing_samples = 0;
    std::uint64_t unknown_gaps = 0;
    std::uint64_t duplicate_or_late_blocks = 0;
    std::uint64_t overlap_blocks = 0;
};

// Publication and consumer operations are thread-safe; queues are bounded and
// never wait for disk/UI. Block metadata is copied before publication.
class SignalHub {
public:
    SignalHub(CaptureDescriptor, const MemoryBudget&);
    Result<std::shared_ptr<SignalConsumer>> addConsumer(ConsumerKind, ConsumerLimits);
    void removeConsumer(const std::shared_ptr<SignalConsumer>&); // Stop input and release the hub's reference.
    PublishResult publish(const SampleBlock&);
    // Known gaps must begin at the expected index; unknown gaps have a null count.
    std::optional<DeviceError> reportGap(const SignalGap&);
    // Call only on real evidence of a discontinuity; adapter-generated indices
    // cannot reveal missing device samples or their length by themselves.
    std::optional<DeviceError> reportUnknownGap(const StreamKey&, std::string reason);
    std::vector<SignalGap> pendingGaps() const; // Includes terminal gaps with no later block.
    void close(); // Reject late publications and freeze each consumer's input.
    HubStats stats() const;
private:
    struct StreamState {
        StreamDescriptor descriptor;
        std::uint64_t next_index = 0;
        std::optional<std::uint64_t> last_timestamp;
        std::optional<SignalGap> pending_known_gap;
        std::optional<SignalGap> pending_unknown_gap;
        bool clock_fault = false;
    };
    CaptureDescriptor capture_;
    MemoryBudget budget_;
    mutable std::mutex mutex_;
    std::map<StreamKey, StreamState> streams_;
    void pruneConsumers(); // Called under mutex_. Subscribers own their lifetime.
    std::vector<std::weak_ptr<SignalConsumer>> consumers_;
    HubStats stats_;
    bool closed_ = false;
};

} // namespace bf::acquisition
