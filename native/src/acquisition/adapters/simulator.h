#pragma once

#include "acquisition/model/signal_model.h"

#include <set>
#include <array>

namespace bf::acquisition {

enum class SimWaveform { Sine, Step, Pulse, IcaMixture };
struct SimChannel {
    ChannelDescriptor descriptor;
    SimWaveform waveform = SimWaveform::Sine;
    double amplitude = 1.0;
    double offset = 0.0;
    double frequency_hz = 1.0;
    std::uint64_t step_at = 0;
    std::uint64_t pulse_period = 1;
    std::uint64_t pulse_width = 1;
    std::array<double, 3> mixing = {1, 0, 0};
};
struct SimStream {
    std::string name;
    Rate rate;
    DType dtype = DType::Int16;
    std::uint32_t samples_per_step = 1;
    double clock_drift_ppm = 0.0;
    std::vector<SimChannel> channels;
    std::string signal_type; // Empty: default to the first configured channel's type.
};
struct SimulatorFaults {
    std::set<std::uint64_t> drop_steps;
    std::set<std::uint64_t> duplicate_steps;
    std::map<std::uint64_t, std::int64_t> clock_jump_ticks;
};
struct SimulatorStep {
    std::vector<SampleBlock> blocks;
    std::vector<SignalEvent> events;
    std::optional<DeviceError> error;
};

// Continuous mathematical sources in microvolts: oscillation, blink, muscle burst.
// Fixed weights and sample time make every block and rate reproducible.
SimStream icaSimulatorStream(int sampleRate);

// step() is deterministic and performs no I/O; a controller schedules it.
// Clock ticks are synthetic nanoseconds, source=Estimated, simulated=true.
// Forward data.gap events to SignalHub::reportGap; a clock.discontinuity event
// requires the controller to end this capture and prepare a new one.
class InProcessSimulator {
public:
    InProcessSimulator(std::vector<SimStream>, const MemoryBudget&);
    InProcessSimulator(const InProcessSimulator&) = delete;
    InProcessSimulator& operator=(const InProcessSimulator&) = delete;
    DeviceDescriptor deviceDescriptor() const;
    std::vector<StreamDescriptor> descriptors() const;
    std::optional<DeviceError> prepare(const CaptureDescriptor&);
    std::optional<DeviceError> start(const CaptureId&);
    std::optional<DeviceError> stop(const CaptureId&);
    SimulatorStep step();
    void setFaults(SimulatorFaults faults);
private:
    std::vector<SimStream> config_;
    DeviceDescriptor device_;
    MemoryBudget budget_;
    std::optional<CaptureDescriptor> capture_;
    std::optional<CaptureId> last_stopped_;
    bool streaming_ = false;
    std::uint64_t step_index_ = 0;
    std::int64_t clock_offset_ticks_ = 0;
    std::vector<std::uint64_t> next_sample_;
    SimulatorFaults faults_;
};

} // namespace bf::acquisition
