#pragma once

#include "acquisition/control/acquisition_controller.h"
#include "render/waveform_slice.h"
#include <optional>

namespace bf {
WaveformSlice acquisitionSignalWindow(const acquisition::CaptureDescriptor&, const std::shared_ptr<acquisition::SignalConsumer>&, const QString& stream, double seconds = 10);

// A failed connection is distinct from a failed start or interrupted stream.
const char* acquisitionFailureStatusKey(const AcquisitionViewSnapshot& view);

// Called on the acquisition worker. Returns a bounded ten-second min/max
// envelope for at most 16 channels, preserving physical units and gaps.
std::vector<AcquisitionTrace> buildAcquisitionTraces(
    const acquisition::StreamDescriptor& descriptor,
    const std::shared_ptr<acquisition::SignalConsumer>& history);

// Stream order follows the capture descriptor; the 16-channel limit is shared
// across all streams. Each trace uses its own stream rate, unit, and gaps.
std::vector<AcquisitionTrace> buildAcquisitionTraces(
    const acquisition::CaptureDescriptor& capture,
    const std::shared_ptr<acquisition::SignalConsumer>& history);

struct AcquisitionTraceTimeRange {
    double first = 0;
    double last = 0;
};
// Shared min/max domain of the displayed streams, including singleton traces.
// Sample-relative positions do not establish clock or hardware synchronization.
std::optional<AcquisitionTraceTimeRange> acquisitionTraceTimeRange(
    const std::vector<AcquisitionTrace>& traces);

// Bridge into the existing file/live waveform renderer. Explicit coordinates
// and NaN breaks retain envelope extrema, independent stream rates and gaps.
WaveformSlice acquisitionWaveformSlice(const std::vector<AcquisitionTrace>& traces);

} // namespace bf
