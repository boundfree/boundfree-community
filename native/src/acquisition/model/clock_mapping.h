#pragma once

#include "acquisition/model/signal_model.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace bf::acquisition {

// t0/t3 share a host monotonic clock; t1/t2 share one device clock.
struct TimeSyncInput {
    std::uint64_t t0_host_ns = 0;
    std::uint64_t t1_device_ticks = 0;
    std::uint64_t t2_device_ticks = 0;
    std::uint64_t t3_host_ns = 0;
    std::uint64_t ticks_per_second_num = 0;
    std::uint64_t ticks_per_second_den = 0;
    // Observed timestamp uncertainty, when independently known. Null is unknown.
    std::optional<double> timestamp_uncertainty_ns;
};
struct TimeSyncMeasurement {
    TimeSyncInput input; // Preserve all original ticks and host times.
    double round_trip_ns = 0;
    // Host time minus nominal device time; its epoch depends on both clocks.
    double offset_ns = 0;
};

// Reject reversed timestamps, negative RTT, invalid frequencies and nanosecond
// conversion overflow. Offset is an estimate assuming midpoint path delay.
Result<TimeSyncMeasurement> measureTimeSync(const TimeSyncInput&);

struct TimeSyncEstimate {
    std::size_t measurement_count = 0;
    std::size_t selected_count = 0;
    std::uint64_t device_reference_ticks = 0;
    std::uint64_t host_reference_ns = 0;
    // host = host_reference_ns + host_reference_offset_ns
    //        + (device_ticks - device_reference_ticks) * seconds_per_tick * 1e9.
    // Use the anchored form rather than subtracting two large absolute doubles.
    double host_reference_offset_ns = 0;
    double seconds_per_tick = 0;
    double offset_ns = 0; // Approximate intercept at device tick zero.
    double minimum_round_trip_ns = 0;
    std::optional<double> drift_ppm;
    std::optional<double> fit_residual_ns;
    // Diagnostic estimate, not a guaranteed accuracy bound. It is populated
    // only with enough fit points and known input timestamp uncertainty.
    std::optional<double> estimated_uncertainty_ns;
};

// Keep at most 32 observations, select the lower-RTT half (at least four when
// available), and fit only with >=4 selected observations spanning >=1 second.
// Conflicting frequencies, clock reset and out-of-order observations are errors;
// reset() starts a new mapping segment after a clock discontinuity.
class ClockSyncEstimator {
public:
    explicit ClockSyncEstimator(std::size_t capacity = 32);
    std::optional<DeviceError> add(const TimeSyncInput&);
    std::optional<TimeSyncEstimate> estimate() const;
    void reset();
    std::size_t size() const { return measurements_.size(); }
private:
    std::size_t capacity_;
    std::deque<TimeSyncMeasurement> measurements_;
};

} // namespace bf::acquisition
