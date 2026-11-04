#include "acquisition/model/clock_mapping.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace bf::acquisition {
namespace {
DeviceError clockError(const char* code) { return {code, {}, std::nullopt, false, "time.sync"}; }
long double signedDifference(std::uint64_t value, std::uint64_t reference) {
    return value >= reference ? static_cast<long double>(value - reference)
                              : -static_cast<long double>(reference - value);
}
long double nsPerTick(const TimeSyncInput& input) {
    return static_cast<long double>(input.ticks_per_second_den) * 1000000000.0L /
           static_cast<long double>(input.ticks_per_second_num);
}
bool sameFrequency(const TimeSyncInput& a, const TimeSyncInput& b) {
    const auto gcd_a = std::gcd(a.ticks_per_second_num, a.ticks_per_second_den);
    const auto gcd_b = std::gcd(b.ticks_per_second_num, b.ticks_per_second_den);
    return a.ticks_per_second_num / gcd_a == b.ticks_per_second_num / gcd_b &&
           a.ticks_per_second_den / gcd_a == b.ticks_per_second_den / gcd_b;
}
} // namespace

Result<TimeSyncMeasurement> measureTimeSync(const TimeSyncInput& input) {
    if (!input.ticks_per_second_num || !input.ticks_per_second_den ||
        (input.timestamp_uncertainty_ns &&
         (!std::isfinite(*input.timestamp_uncertainty_ns) || *input.timestamp_uncertainty_ns < 0)))
        return {std::nullopt, clockError("clock.invalid_parameters")};
    if (input.t3_host_ns < input.t0_host_ns || input.t2_device_ticks < input.t1_device_ticks)
        return {std::nullopt, clockError("clock.out_of_order")};
    const auto period_ns = nsPerTick(input);
    const auto device_begin_ns = static_cast<long double>(input.t1_device_ticks) * period_ns;
    const auto device_duration_ns = static_cast<long double>(input.t2_device_ticks - input.t1_device_ticks) * period_ns;
    const auto device_end_ns = static_cast<long double>(input.t2_device_ticks) * period_ns;
    const auto ns_limit = std::ldexp(1.0L, 64);
    if (!std::isfinite(period_ns) || !std::isfinite(device_begin_ns) || !std::isfinite(device_duration_ns) ||
        !std::isfinite(device_end_ns) || device_begin_ns >= ns_limit || device_duration_ns >= ns_limit ||
        device_end_ns >= ns_limit)
        return {std::nullopt, clockError("clock.overflow")};
    const auto host_duration_ns = static_cast<long double>(input.t3_host_ns - input.t0_host_ns);
    const auto rtt_ns = host_duration_ns - device_duration_ns;
    if (rtt_ns < 0) return {std::nullopt, clockError("clock.negative_rtt")};
    const auto offset_ns = static_cast<long double>(input.t0_host_ns) + rtt_ns / 2.0L - device_begin_ns;
    if (!std::isfinite(offset_ns) || std::abs(offset_ns) > std::numeric_limits<double>::max())
        return {std::nullopt, clockError("clock.overflow")};
    return {TimeSyncMeasurement{input, static_cast<double>(rtt_ns), static_cast<double>(offset_ns)}, std::nullopt};
}

ClockSyncEstimator::ClockSyncEstimator(std::size_t capacity) : capacity_(capacity) {
    if (!capacity || capacity > 32) throw std::invalid_argument("clock measurement capacity must be 1..32");
}
std::optional<DeviceError> ClockSyncEstimator::add(const TimeSyncInput& input) {
    auto measured = measureTimeSync(input);
    if (!measured) return measured.error;
    if (!measurements_.empty()) {
        const auto& last = measurements_.back().input;
        if (!sameFrequency(last, input)) return clockError("clock.frequency_changed");
        if (input.t0_host_ns < last.t0_host_ns) return clockError("clock.out_of_order");
        if (input.t1_device_ticks < last.t1_device_ticks) return clockError("clock.discontinuity");
    }
    if (measurements_.size() == capacity_) measurements_.pop_front();
    measurements_.push_back(std::move(*measured.value));
    return std::nullopt;
}
void ClockSyncEstimator::reset() { measurements_.clear(); }
std::optional<TimeSyncEstimate> ClockSyncEstimator::estimate() const {
    if (measurements_.empty()) return std::nullopt;
    std::vector<const TimeSyncMeasurement*> selected;
    selected.reserve(measurements_.size());
    for (const auto& measurement : measurements_) selected.push_back(&measurement);
    std::stable_sort(selected.begin(), selected.end(), [](const auto* a, const auto* b) {
        return a->round_trip_ns < b->round_trip_ns;
    });
    selected.resize(std::min(selected.size(), std::max<std::size_t>(4, selected.size() / 2)));
    const auto& best = *selected.front();
    const auto reference_ticks = best.input.t1_device_ticks;
    const auto reference_host = best.input.t0_host_ns;
    const auto period_ns = nsPerTick(best.input);
    long double intercept = static_cast<long double>(best.round_trip_ns) / 2.0L;
    long double slope = 1.0L;
    TimeSyncEstimate result;
    result.measurement_count = measurements_.size(); result.selected_count = selected.size();
    result.device_reference_ticks = reference_ticks; result.host_reference_ns = reference_host;
    result.minimum_round_trip_ns = best.round_trip_ns;
    if (selected.size() >= 4) {
        std::vector<long double> x, y;
        x.reserve(selected.size()); y.reserve(selected.size());
        for (const auto* point : selected) {
            x.push_back(signedDifference(point->input.t1_device_ticks, reference_ticks) * period_ns);
            y.push_back(signedDifference(point->input.t0_host_ns, reference_host) +
                        static_cast<long double>(point->round_trip_ns) / 2.0L);
        }
        const auto range = std::minmax_element(x.begin(), x.end());
        if (*range.second - *range.first >= 1000000000.0L) {
            const long double mean_x = std::accumulate(x.begin(), x.end(), 0.0L) / x.size();
            const long double mean_y = std::accumulate(y.begin(), y.end(), 0.0L) / y.size();
            long double xx = 0, xy = 0;
            for (std::size_t i = 0; i < x.size(); ++i) {
                xx += (x[i] - mean_x) * (x[i] - mean_x);
                xy += (x[i] - mean_x) * (y[i] - mean_y);
            }
            if (xx > 0) {
                const auto candidate_slope = xy / xx;
                if (std::isfinite(candidate_slope) && candidate_slope > 0) {
                    slope = candidate_slope;
                    intercept = mean_y - slope * mean_x;
                    long double residual_sum = 0;
                    bool known_uncertainty = true;
                    double max_uncertainty = 0;
                    for (std::size_t i = 0; i < x.size(); ++i) {
                        const auto residual = y[i] - (intercept + slope * x[i]);
                        residual_sum += residual * residual;
                        if (!selected[i]->input.timestamp_uncertainty_ns) known_uncertainty = false;
                        else max_uncertainty = std::max(max_uncertainty, *selected[i]->input.timestamp_uncertainty_ns);
                    }
                    const auto residual_rms = std::sqrt(residual_sum / x.size());
                    result.drift_ppm = static_cast<double>((slope - 1.0L) * 1000000.0L);
                    result.fit_residual_ns = static_cast<double>(residual_rms);
                    if (known_uncertainty) {
                        const auto diagnostic = residual_rms + static_cast<long double>(best.round_trip_ns) / 2.0L + max_uncertainty;
                        if (std::isfinite(diagnostic) && diagnostic <= std::numeric_limits<double>::max())
                            result.estimated_uncertainty_ns = static_cast<double>(diagnostic);
                    }
                }
            }
        }
    }
    result.host_reference_offset_ns = static_cast<double>(intercept);
    result.seconds_per_tick = static_cast<double>(period_ns * slope / 1000000000.0L);
    result.offset_ns = static_cast<double>(static_cast<long double>(reference_host) + intercept -
                                            static_cast<long double>(reference_ticks) * period_ns * slope);
    return result;
}
} // namespace bf::acquisition
