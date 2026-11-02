#include "acquisition/session/acquisition_session.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace bf::acquisition {
namespace {
DeviceError sessionError(const char* code) { return {code, {}, std::nullopt, false, "session"}; }
}

AcquisitionSession::AcquisitionSession(DeviceDescriptor device, SessionId session_id, Generation generation)
    : device_(std::move(device)), session_id_(std::move(session_id)), generation_(generation) {
    if (device_.device_id.value.empty() || device_.boot_id.value.empty() || session_id_.value.empty())
        throw std::invalid_argument("session requires device, boot and session identities");
}

Result<CaptureDescriptor> AcquisitionSession::prepare(std::vector<StreamDescriptor> templates) {
    if (state_ != SessionState::Idle) return {std::nullopt, sessionError("device.busy")};
    if (templates.empty() || templates.size() > 64 ||
        next_stream_id_ + templates.size() - 1 > std::numeric_limits<std::uint32_t>::max())
        return {std::nullopt, sessionError("acquisition.stream_limit")};
    CaptureDescriptor candidate{session_id_, CaptureId{newIdentity()}, generation_, {}};
    for (std::size_t i = 0; i < templates.size(); ++i) {
        auto& descriptor = templates[i];
        descriptor.key = {session_id_, static_cast<std::uint32_t>(next_stream_id_ + i)};
        descriptor.capture_id = candidate.capture_id;
        descriptor.generation = generation_;
        descriptor.revision = 1;
        if (auto invalid = validateStreamDescriptor(descriptor)) return {std::nullopt, *invalid};
    }
    next_stream_id_ += templates.size();
    candidate.streams = std::move(templates);
    capture_ = candidate;
    state_ = SessionState::Prepared;
    return {candidate, std::nullopt};
}

std::optional<DeviceError> AcquisitionSession::start(const CaptureId& capture_id) {
    if (!capture_ || capture_->capture_id != capture_id) return sessionError("acquisition.stale_capture");
    if (state_ == SessionState::Streaming) return std::nullopt;
    if (state_ != SessionState::Prepared) return sessionError("device.invalid_state");
    state_ = SessionState::Streaming;
    return std::nullopt;
}

std::optional<DeviceError> AcquisitionSession::stop(const CaptureId& capture_id) {
    if (last_stopped_ && *last_stopped_ == capture_id && state_ == SessionState::Idle) return std::nullopt;
    if (!capture_ || capture_->capture_id != capture_id) return sessionError("acquisition.stale_capture");
    if (state_ != SessionState::Prepared && state_ != SessionState::Streaming) return sessionError("device.invalid_state");
    last_stopped_ = capture_id;
    capture_.reset();
    state_ = SessionState::Idle;
    return std::nullopt;
}

void AcquisitionSession::invalidate() {
    if (generation_ == std::numeric_limits<Generation>::max()) throw std::overflow_error("generation exhausted");
    ++generation_;
    capture_.reset();
    last_stopped_.reset();
    state_ = SessionState::Idle;
}

bool AcquisitionSession::accepts(const SampleBlock& block) const {
    if (state_ != SessionState::Streaming || !capture_) return false;
    const auto it = std::find_if(capture_->streams.begin(), capture_->streams.end(), [&](const auto& descriptor) {
        return descriptor.key == block.stream;
    });
    return it != capture_->streams.end() && !validateSampleBlock(*it, block);
}
} // namespace bf::acquisition
