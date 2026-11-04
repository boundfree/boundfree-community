#include "acquisition/model/clock_mapping.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace bf::acquisition;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("check failed at line ") + std::to_string(__LINE__) + ": " #x); } while (false)

namespace {
TimeSyncInput sample(std::uint64_t index, bool high_latency, bool known_uncertainty) {
    const std::uint64_t device_tick = 2000000000ULL + index * 1000000000ULL;
    const std::uint64_t actual_host = 10000000000ULL + index * 1000100000ULL;
    const std::uint64_t outbound = high_latency ? 20000000ULL : 1000000ULL;
    const std::uint64_t rtt = high_latency ? 100000000ULL : 2000000ULL;
    TimeSyncInput input;
    input.t0_host_ns = actual_host - outbound;
    input.t1_device_ticks = device_tick;
    input.t2_device_ticks = device_tick;
    input.t3_host_ns = input.t0_host_ns + rtt;
    input.ticks_per_second_num = 1000000000ULL;
    input.ticks_per_second_den = 1;
    if (known_uncertainty) input.timestamp_uncertainty_ns = 2000.0;
    return input;
}
void measurementValidation() {
    TimeSyncInput input{1000000000ULL, 100, 102, 1010000000ULL, 1000, 1, std::nullopt};
    auto result = measureTimeSync(input);
    CHECK(result && result.value->round_trip_ns == 8000000.0 && result.value->offset_ns == 904000000.0);
    CHECK(result.value->input.t1_device_ticks == 100 && result.value->input.t3_host_ns == 1010000000ULL);
    auto invalid = input; invalid.t3_host_ns = input.t0_host_ns - 1;
    CHECK(!measureTimeSync(invalid) && measureTimeSync(invalid).error->code == "clock.out_of_order");
    invalid = input; invalid.t2_device_ticks = input.t1_device_ticks - 1;
    CHECK(!measureTimeSync(invalid) && measureTimeSync(invalid).error->code == "clock.out_of_order");
    invalid = input; invalid.t3_host_ns = input.t0_host_ns + 1000000;
    CHECK(!measureTimeSync(invalid) && measureTimeSync(invalid).error->code == "clock.negative_rtt");
    invalid = input; invalid.ticks_per_second_den = 0;
    CHECK(!measureTimeSync(invalid) && measureTimeSync(invalid).error->code == "clock.invalid_parameters");
    invalid = input; invalid.timestamp_uncertainty_ns = std::numeric_limits<double>::quiet_NaN();
    CHECK(!measureTimeSync(invalid));
    invalid = input; invalid.timestamp_uncertainty_ns = -1;
    CHECK(!measureTimeSync(invalid));
    invalid = input; invalid.t1_device_ticks = invalid.t2_device_ticks = std::numeric_limits<std::uint64_t>::max();
    invalid.ticks_per_second_num = invalid.ticks_per_second_den = 1;
    CHECK(!measureTimeSync(invalid) && measureTimeSync(invalid).error->code == "clock.overflow");
    invalid = input; invalid.t0_host_ns = std::numeric_limits<std::uint64_t>::max() - 1000;
    invalid.t3_host_ns = std::numeric_limits<std::uint64_t>::max();
    invalid.t1_device_ticks = 4000; invalid.t2_device_ticks = 4001;
    invalid.ticks_per_second_num = 1000000000; invalid.ticks_per_second_den = 1;
    auto near_end = measureTimeSync(invalid);
    CHECK(near_end && near_end.value->round_trip_ns == 999.0); // No t0+t3 overflow.
}
void estimationAndQuality() {
    ClockSyncEstimator estimator;
    CHECK(!estimator.estimate());
    for (std::uint64_t i = 0; i < 3; ++i) CHECK(!estimator.add(sample(i, false, false)));
    auto insufficient = estimator.estimate();
    CHECK(insufficient && !insufficient->drift_ppm && !insufficient->estimated_uncertainty_ns);
    for (std::uint64_t i = 3; i < 32; ++i) CHECK(!estimator.add(sample(i, i % 2 == 1, false)));
    auto unknown = estimator.estimate();
    CHECK(unknown && unknown->measurement_count == 32 && unknown->selected_count == 16);
    CHECK(unknown->drift_ppm && std::abs(*unknown->drift_ppm - 100.0) < 0.01);
    CHECK(unknown->fit_residual_ns && !unknown->estimated_uncertainty_ns);
    CHECK(std::abs(unknown->minimum_round_trip_ns - 2000000.0) < 0.01);
    const auto host_anchor = static_cast<long double>(unknown->host_reference_ns) + unknown->host_reference_offset_ns;
    const auto future_ticks = unknown->device_reference_ticks + 3000000000ULL;
    const auto mapped = host_anchor + static_cast<long double>(future_ticks - unknown->device_reference_ticks) *
                        unknown->seconds_per_tick * 1000000000.0L;
    CHECK(std::abs(mapped - 13000300000.0L) < 10.0L);
    CHECK(!estimator.add(sample(32, false, false)) && estimator.size() == 32);
    auto before = estimator.size();
    auto changed = sample(33, false, false); changed.ticks_per_second_num = 999999999;
    CHECK(estimator.add(changed)->code == "clock.frequency_changed" && estimator.size() == before);
    auto reset = sample(33, false, false); reset.t1_device_ticks = reset.t2_device_ticks = 0;
    CHECK(estimator.add(reset)->code == "clock.discontinuity" && estimator.size() == before);
    auto late = sample(1, false, false);
    CHECK(estimator.add(late)->code == "clock.out_of_order" && estimator.size() == before);
    estimator.reset(); CHECK(estimator.size() == 0 && !estimator.estimate());
    for (std::uint64_t i = 0; i < 32; ++i) CHECK(!estimator.add(sample(i, i % 2 == 1, true)));
    auto known = estimator.estimate();
    CHECK(known && known->estimated_uncertainty_ns &&
          *known->estimated_uncertainty_ns >= 1002000.0 && *known->estimated_uncertainty_ns < 1002010.0);
    auto equivalent = sample(32, false, true);
    equivalent.ticks_per_second_num = 2000000000ULL; equivalent.ticks_per_second_den = 2;
    CHECK(!estimator.add(equivalent));
}
void sparseAndBounded() {
    ClockSyncEstimator estimator(4);
    for (std::uint64_t i = 0; i < 4; ++i) {
        auto input = sample(i, false, true);
        input.t0_host_ns = 10000000000ULL + i * 100000000ULL;
        input.t3_host_ns = input.t0_host_ns + 2000000ULL;
        input.t1_device_ticks = input.t2_device_ticks = 2000000000ULL + i * 100000000ULL;
        CHECK(!estimator.add(input));
    }
    const auto estimate = estimator.estimate();
    CHECK(estimate && !estimate->drift_ppm && !estimate->estimated_uncertainty_ns);
    bool rejected = false;
    try { ClockSyncEstimator excessive(33); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}
} // namespace
int main() {
    try {
        measurementValidation(); estimationAndQuality(); sparseAndBounded();
        std::cout << "clock_mapping_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "clock_mapping_test: " << error.what() << '\n';
        return 1;
    }
}
