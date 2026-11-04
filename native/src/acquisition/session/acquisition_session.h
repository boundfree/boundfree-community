#pragma once

#include "acquisition/model/signal_model.h"

namespace bf::acquisition {

enum class SessionState { Idle, Prepared, Streaming };

// Serialize all session operations on one acquisition worker. UI must not call
// blocking device operations through this class on the GUI thread. Supply a new
// session_id after each connection; invalidate() rejects stale local callbacks.
class AcquisitionSession {
public:
    AcquisitionSession(DeviceDescriptor device, SessionId session_id, Generation generation);
    AcquisitionSession(const AcquisitionSession&) = delete;
    AcquisitionSession& operator=(const AcquisitionSession&) = delete;
    Result<CaptureDescriptor> prepare(std::vector<StreamDescriptor> templates);
    std::optional<DeviceError> start(const CaptureId&);
    std::optional<DeviceError> stop(const CaptureId&); // Idempotent for the last stopped capture.
    void invalidate(); // Advances generation and invalidates all prior callbacks.
    bool accepts(const SampleBlock&) const;
    SessionState state() const { return state_; }
    Generation generation() const { return generation_; }
    const DeviceDescriptor& device() const { return device_; }
    const std::optional<CaptureDescriptor>& capture() const { return capture_; }
private:
    DeviceDescriptor device_;
    SessionId session_id_;
    Generation generation_;
    SessionState state_ = SessionState::Idle;
    std::uint64_t next_stream_id_ = 1;
    std::optional<CaptureDescriptor> capture_;
    std::optional<CaptureId> last_stopped_;
};

} // namespace bf::acquisition
