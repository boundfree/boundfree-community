#pragma once

#include "controller/capture_recorder.h"
#include <vector>

namespace bf {

enum class DeviceConnectionState { Disconnected, Connecting, Connected, Faulted };
enum class DeviceAcquisitionState { Idle, Starting, Streaming, Stopping, Faulted };

struct AcquisitionTracePoint {
    double seconds = 0;
    double value = 0;
    bool gapBefore = false;
};
struct AcquisitionTrace {
    QString name;
    QString unit; // Empty means unknown; each channel has its own scale.
    acquisition::Rate rate{0, 0};
    bool multipleStreams = false;
    std::vector<AcquisitionTracePoint> points;
};
struct AcquisitionViewSnapshot {
    DeviceConnectionState connection = DeviceConnectionState::Disconnected;
    DeviceAcquisitionState acquisition = DeviceAcquisitionState::Idle;
    CaptureRecordSnapshot recording;
    bool simulated = false;
    bool canConfigure = false;
    bool configurationInProgress = false;
    bool configurationVerified = true;
    QString connectionPhase; // Stable phase suffix, translated only by the UI.
    quint64 connectionRevision = 0; // Worker has begun this many connection attempts.
    quint64 disconnectionCompletion = 0; // Last completed close request, including no-ops.
    quint32 configRevision = 0; // Last device-confirmed configuration revision.
    quint64 configCompletion = 0; // Local terminal-operation counter for coalesced notifications.
    quint64 acquisitionCompletion = 0; // Local terminal result, including cleanup or a rejected/recovered request.
    int sampleRate = 250;
    acquisition::Rate exactRate{250, 1};
    std::size_t totalChannels = 0;
    quint64 samples = 0;
    quint64 missingSamples = 0;
    quint64 unknownGaps = 0;
    QString deviceName;
    QString diagnosticLogPath; // Current/latest connection log, retained after stop/disconnect.
    AppError error;
    AppError warning; // Non-blocking, translated by the UI.
    std::vector<AcquisitionTrace> traces;
};

} // namespace bf
