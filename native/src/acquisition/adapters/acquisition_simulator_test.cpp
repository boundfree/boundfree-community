#include "acquisition/adapters/simulator.h"
#include "acquisition/buffer/signal_hub.h"
#include "acquisition/session/acquisition_session.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace bf::acquisition;

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("check failed at line ") + std::to_string(__LINE__) + ": " #x); } while (false)

namespace {
SimChannel channel(const char* name, SimWaveform wave) {
    SimChannel result;
    result.descriptor.channel_id = name;
    result.descriptor.name = name;
    result.descriptor.signal_type = "eeg";
    result.descriptor.unit = Unit::Microvolt;
    result.waveform = wave;
    return result;
}
SimStream firstStream() {
    auto sine = channel("sine", SimWaveform::Sine); sine.amplitude = 100; sine.frequency_hz = 1;
    auto step = channel("step", SimWaveform::Step); step.amplitude = 7; step.offset = 3; step.step_at = 2;
    auto pulse = channel("pulse", SimWaveform::Pulse); pulse.amplitude = 10; pulse.pulse_period = 3; pulse.pulse_width = 1;
    SimStream s; s.name = "eeg"; s.rate = {4, 1}; s.dtype = DType::Int24; s.samples_per_step = 4;
    s.channels = {sine, step, pulse};
    return s;
}
SimStream secondStream() {
    auto sine = channel("slow", SimWaveform::Sine); sine.frequency_hz = 0.5;
    SimStream s; s.name = "slow"; s.rate = {2, 1}; s.dtype = DType::Float64; s.samples_per_step = 2;
    s.clock_drift_ppm = 1000; s.channels = {sine};
    return s;
}
void feedGaps(SignalHub& hub, const SimulatorStep& step) {
    for (const auto& event : step.events) if (event.gap) CHECK(!hub.reportGap(*event.gap));
}
void waveformGapAndFanout() {
    MemoryBudget budget(65536);
    {
        InProcessSimulator sim({firstStream(), secondStream()}, budget);
        const auto device = sim.deviceDescriptor();
        CHECK(device.boot_id == sim.deviceDescriptor().boot_id && device.generated_boot_id);
        AcquisitionSession session(device, SessionId{"session"}, 1);
        auto prepared = session.prepare(sim.descriptors());
        CHECK(prepared);
        const auto capture = *prepared.value;
        CHECK(capture.streams[0].signal_type == "eeg"); // Default from the first channel.
        CHECK(capture.streams[0].clock.source == ClockSource::Estimated && capture.streams[0].clock.simulated);
        CHECK(!sim.prepare(capture));
        CHECK(!session.start(capture.capture_id) && !sim.start(capture.capture_id));
        CHECK(!sim.start(capture.capture_id));
        SignalHub hub(capture, budget);
        auto history_result = hub.addConsumer(ConsumerKind::History, {64, 2});
        auto recorder_result = hub.addConsumer(ConsumerKind::Recording, {64, 2});
        CHECK(history_result && recorder_result);
        auto history = *history_result.value;
        auto recorder = *recorder_result.value;
        SimulatorFaults faults; faults.drop_steps.insert(1); faults.duplicate_steps.insert(2); sim.setFaults(faults);
        auto first = sim.step();
        CHECK(!first.error && first.blocks.size() == 2);
        const auto& vals = *first.blocks[0].storage->data<std::int32_t>();
        CHECK(vals == std::vector<std::int32_t>({0, 100, 0, -100, 3, 3, 10, 10, 10, 0, 0, 10}));
        const auto& slow = *first.blocks[1].storage->data<double>();
        CHECK(std::abs(slow[0]) < 1e-12 && std::abs(slow[1] - 1.0) < 1e-12);
        for (const auto& b : first.blocks) CHECK(session.accepts(b) && hub.publish(b).status == PublishStatus::Accepted);
        auto lost = sim.step();
        CHECK(lost.blocks.empty() && lost.events.size() == 2);
        feedGaps(hub, lost);
        CHECK(hub.stats().known_missing_samples == 6 && hub.pendingGaps().size() == 2);
        auto next = sim.step();
        CHECK(!next.error && next.blocks.size() == 4);
        CHECK(next.blocks[0].first_sample_index == 8 && next.blocks[2].first_sample_index == 4);
        CHECK(*next.blocks[0].first_timestamp_ticks == 2000000000ULL);
        // MSVC represents long double as double; conversion truncates a
        // fractional nanosecond. Preserve the one-tick quantization bound.
        CHECK(*next.blocks[2].first_timestamp_ticks >= 2001999999ULL &&
              *next.blocks[2].first_timestamp_ticks <= 2002000000ULL);
        CHECK(hub.publish(next.blocks[0]).status == PublishStatus::Accepted);
        CHECK(hub.publish(next.blocks[1]).status == PublishStatus::DuplicateOrLate);
        CHECK(hub.publish(next.blocks[2]).status == PublishStatus::Accepted);
        CHECK(hub.publish(next.blocks[3]).status == PublishStatus::DuplicateOrLate);
        CHECK(hub.stats().known_missing_samples == 6 && hub.stats().duplicate_or_late_blocks == 2);
        CHECK(hub.stats().accepted_samples == 12 && hub.pendingGaps().empty());
        auto r = recorder->stats();
        CHECK(r.state == ConsumerState::Failed && r.accepted_samples == 6 && r.rejected_samples == 6);
        CHECK(r.first_rejected && r.first_rejected->first_sample_index == 8 && r.first_rejected->count == 4);
        CHECK(r.last_accepted_exclusive.at(capture.streams[0].key) == 4);
        CHECK(r.last_accepted_exclusive.at(capture.streams[1].key) == 2);
        CHECK(r.known_missing_samples == 6);
        auto h = history->stats();
        CHECK(h.state == ConsumerState::Active && h.skipped_samples == 6 && h.queued_bytes == 64);
        auto snapshot = history->snapshot();
        CHECK(snapshot.size() == 2 && snapshot[0].gaps_before.size() == 1 && snapshot[0].gaps_before[0].count == 4);
        CHECK(snapshot[1].gaps_before.size() == 1 && snapshot[1].gaps_before[0].count == 2);
        CHECK(!hub.reportUnknownGap(capture.streams[0].key, "sdk.disconnect"));
        CHECK(hub.stats().unknown_gaps == 1 && history->stats().unknown_gaps == 1);
        CHECK(hub.pendingGaps().size() == 1 && !hub.pendingGaps()[0].count);
        CHECK(hub.reportGap(SignalGap{capture.streams[0].key, 0, 0, "invalid"}));
        CHECK(!sim.stop(capture.capture_id) && !sim.stop(capture.capture_id));
        CHECK(!session.stop(capture.capture_id) && !session.stop(capture.capture_id));
        hub.close();
        CHECK(hub.publish(next.blocks[0]).status == PublishStatus::Stale);
        CHECK(recorder->stats().state == ConsumerState::Failed);
        CHECK(history->stats().state == ConsumerState::Stopped);
    }
    CHECK(budget.used() == 0);
}
void memoryPressureAndTerminalGap() {
    MemoryBudget budget(544);
    auto config = secondStream(); config.clock_drift_ppm = 0;
    InProcessSimulator sim({config}, budget);
    AcquisitionSession session(sim.deviceDescriptor(), SessionId{"pressure"}, 7);
    auto prep = session.prepare(sim.descriptors()); CHECK(prep);
    auto capture = *prep.value;
    CHECK(!sim.prepare(capture) && !sim.start(capture.capture_id) && !session.start(capture.capture_id));
    SignalHub hub(capture, budget);
    auto first = sim.step();
    CHECK(first.blocks.size() == 1 && budget.used() == first.blocks[0].storage->accountedBytes());
    CHECK(hub.publish(first.blocks[0]).status == PublishStatus::Accepted);
    auto failed = sim.step();
    CHECK(failed.error && failed.error->code == "buffer.budget_exhausted" && failed.events.size() == 1);
    feedGaps(hub, failed);
    CHECK(hub.pendingGaps().size() == 1 && hub.pendingGaps()[0].count == 2);
    first.blocks.clear(); CHECK(budget.used() == 0);
    auto next = sim.step(); CHECK(next.blocks.size() == 1 && next.blocks[0].first_sample_index == 4);
    CHECK(hub.publish(next.blocks[0]).status == PublishStatus::Accepted);
    CHECK(hub.stats().known_missing_samples == 2);
    next.blocks.clear(); CHECK(budget.used() == 0);
    CHECK(!hub.reportUnknownGap(capture.streams[0].key, "adapter_counter_unavailable"));
    CHECK(!hub.pendingGaps()[0].count); // A terminal unknown gap survives without a next block.
}
void clockAndCycles() {
    MemoryBudget budget(65536);
    auto config = secondStream(); config.rate = {1, 1}; config.samples_per_step = 1; config.clock_drift_ppm = 0;
    InProcessSimulator sim({config}, budget);
    AcquisitionSession session(sim.deviceDescriptor(), SessionId{"cycles"}, 9);
    CaptureId previous;
    for (int cycle = 0; cycle < 3; ++cycle) {
        auto prep = session.prepare(sim.descriptors()); CHECK(prep);
        auto capture = *prep.value;
        CHECK(cycle == 0 || capture.capture_id != previous);
        CHECK(!sim.prepare(capture) && !session.start(capture.capture_id) && !sim.start(capture.capture_id));
        SignalHub hub(capture, budget);
        SimulatorFaults faults;
        if (cycle == 0) faults.clock_jump_ticks[2] = -1500000000;
        sim.setFaults(faults);
        auto b0 = sim.step(); CHECK(b0.blocks.size() == 1 && b0.blocks[0].first_sample_index == 0);
        CHECK(hub.publish(b0.blocks[0]).status == PublishStatus::Accepted);
        if (cycle == 0) {
            auto b1 = sim.step(); CHECK(hub.publish(b1.blocks[0]).status == PublishStatus::Accepted);
            auto b2 = sim.step();
            CHECK(!b2.events.empty() && b2.events[0].kind == "clock.discontinuity");
            CHECK(hub.publish(b2.blocks[0]).status == PublishStatus::ClockDiscontinuity);
            CHECK(hub.publish(b2.blocks[0]).status == PublishStatus::ClockDiscontinuity);
        }
        CHECK(!session.stop(capture.capture_id) && !sim.stop(capture.capture_id));
        CHECK(!session.accepts(b0.blocks[0]));
        hub.close(); CHECK(hub.publish(b0.blocks[0]).status == PublishStatus::Stale);
        previous = capture.capture_id;
    }
}
void midstreamStartAndUnknownGap() {
    MemoryBudget budget(65536);
    auto config = secondStream(); config.samples_per_step = 1;
    InProcessSimulator sim({config}, budget);
    AcquisitionSession session(sim.deviceDescriptor(), SessionId{"midstream"}, 1);
    auto templates = sim.descriptors();
    // Adapter-generated counters enumerate received samples, not missing device samples.
    templates[0].sample_index_source = SampleIndexSource::AdapterReceived;
    auto prep = session.prepare(templates); CHECK(prep);
    const auto capture = *prep.value;
    SignalHub hub(capture, budget);
    MemoryBudget too_small(1024);
    SignalHub constrained(capture, too_small);
    auto refused = constrained.addConsumer(ConsumerKind::Recording, {64, 1});
    CHECK(!refused && refused.error->code == "buffer.budget_exhausted" && too_small.used() == 0);
    auto makeBlock = [&](std::uint64_t index) {
        SampleBlock b;
        b.stream = capture.streams[0].key; b.capture_id = capture.capture_id; b.generation = capture.generation;
        b.descriptor_revision = 1; b.channel_count = 1; b.sample_count = 1; b.first_sample_index = index;
        b.clock_id = capture.streams[0].clock.clock_id; b.first_timestamp_ticks = index; b.timestamp_estimated = true;
        b.storage = SampleStorage::fromFloat64({static_cast<double>(index)}, budget);
        return b;
    };
    CHECK(hub.publish(makeBlock(0)).status == PublishStatus::Accepted);
    CHECK(!hub.reportUnknownGap(capture.streams[0].key, "sdk.disconnect"));
    auto rec_result = hub.addConsumer(ConsumerKind::Recording, {128, 8}); CHECK(rec_result);
    auto recorder = *rec_result.value;
    CHECK(recorder->stats().start_sample_index.at(capture.streams[0].key) == 1);
    CHECK(recorder->stats().unknown_gaps == 0);
    CHECK(hub.publish(makeBlock(1)).status == PublishStatus::Accepted);
    auto first = recorder->pop(); CHECK(first && first->block->first_sample_index == 1 && first->gaps_before.empty());
    CHECK(!hub.reportUnknownGap(capture.streams[0].key, "sdk.disconnect"));
    CHECK(hub.publish(makeBlock(2)).status == PublishStatus::Accepted);
    auto second = recorder->pop();
    CHECK(second && second->gaps_before.size() == 1 && !second->gaps_before[0].count);
    CHECK(recorder->stats().unknown_gaps == 1 && hub.stats().known_missing_samples == 0);
    auto empty_result = hub.addConsumer(ConsumerKind::Recording, {128, 8}); CHECK(empty_result);
    auto empty = *empty_result.value;
    empty->stop();
    CHECK(empty->stats().start_sample_index.at(capture.streams[0].key) == 3);
    CHECK(empty->stats().last_accepted_exclusive.empty());
    hub.removeConsumer(empty);
    CHECK(!hub.reportGap(SignalGap{capture.streams[0].key, 3, 2, "adapter.reported"}));
    auto later_result = hub.addConsumer(ConsumerKind::Recording, {128, 8}); CHECK(later_result);
    auto later = *later_result.value;
    CHECK(later->stats().start_sample_index.at(capture.streams[0].key) == 5);
    CHECK(hub.publish(makeBlock(5)).status == PublishStatus::Accepted);
    auto later_first = later->pop();
    CHECK(later_first && later_first->gaps_before.empty() && later->stats().known_missing_samples == 0);
    auto old_consumer_next = recorder->pop();
    CHECK(old_consumer_next && old_consumer_next->gaps_before.size() == 1 &&
          old_consumer_next->gaps_before[0].count == 2);
    for (int i = 0; i < 40; ++i) {
        auto temporary = hub.addConsumer(ConsumerKind::Recording, {128, 8}); CHECK(temporary);
        hub.removeConsumer(*temporary.value);
        CHECK((*temporary.value)->stats().state == ConsumerState::Stopped);
    }
}
void subscriptionAndStopRaces() {
    MemoryBudget budget(32 * 1024 * 1024);
    auto config = secondStream(); config.samples_per_step = 1;
    InProcessSimulator sim({config}, budget);
    AcquisitionSession session(sim.deviceDescriptor(), SessionId{"race"}, 1);
    auto prep = session.prepare(sim.descriptors()); CHECK(prep);
    auto capture = *prep.value;
    CHECK(!sim.prepare(capture) && !sim.start(capture.capture_id));
    SignalHub hub(capture, budget);
    std::atomic<int> produced{0};
    std::atomic<bool> go{false}, failed{false};
    std::thread producer([&] {
        for (int i = 0; i < 2000; ++i) {
            if (i == 10) while (!go.load()) std::this_thread::yield();
            auto step = sim.step();
            if (step.error || step.blocks.size() != 1 || hub.publish(step.blocks[0]).status != PublishStatus::Accepted) {
                failed = true; break;
            }
            ++produced;
            if (i % 8 == 0) std::this_thread::yield();
        }
    });
    while (produced.load() < 10 && !failed.load()) std::this_thread::yield();
    auto initial_result = hub.addConsumer(ConsumerKind::Recording, {65536, 4096});
    go = true;
    auto racing_result = hub.addConsumer(ConsumerKind::Recording, {65536, 4096});
    while (produced.load() < 500 && !failed.load()) std::this_thread::yield();
    auto initial = *initial_result.value;
    auto racing = *racing_result.value;
    initial->stop();
    const auto stopped_count = initial->stats().accepted_samples;
    producer.join();
    CHECK(!failed && produced == 2000 && initial->stats().accepted_samples == stopped_count);
    const auto key = capture.streams[0].key;
    for (const auto& consumer : {initial, racing}) {
        const auto stats = consumer->stats();
        const auto items = consumer->snapshot();
        const auto start = stats.start_sample_index.at(key);
        CHECK(start >= 10 && start <= 2000 && stats.accepted_samples == items.size());
        for (std::size_t i = 0; i < items.size(); ++i) CHECK(items[i].block->first_sample_index == start + i);
        if (items.empty()) CHECK(stats.last_accepted_exclusive.empty());
        else CHECK(stats.last_accepted_exclusive.at(key) == start + items.size());
    }
}
void abandonedConsumerReleasesStorage() {
    MemoryBudget budget(65536);
    InProcessSimulator sim({secondStream()}, budget);
    AcquisitionSession session(sim.deviceDescriptor(), SessionId{"release"}, 3);
    auto prep = session.prepare(sim.descriptors()); CHECK(prep);
    const auto capture = *prep.value;
    CHECK(!sim.prepare(capture) && !sim.start(capture.capture_id));
    SignalHub hub(capture, budget);
    for (int i = 0; i < 40; ++i) {
        auto temporary = hub.addConsumer(ConsumerKind::Recording, {64, 1});
        CHECK(temporary); // The last owner is released at each loop iteration.
    }
    {
        auto registration = hub.addConsumer(ConsumerKind::Recording, {64, 1}); CHECK(registration);
        auto consumer = *registration.value;
        const auto queue_reservation = budget.used();
        CHECK(queue_reservation > 0);
        auto first = sim.step(); CHECK(hub.publish(first.blocks[0]).status == PublishStatus::Accepted);
        const auto storage_reservation = first.blocks[0].storage->accountedBytes();
        first.blocks.clear();
        auto second = sim.step(); CHECK(hub.publish(second.blocks[0]).status == PublishStatus::Accepted);
        second.blocks.clear();
        CHECK(consumer->stats().state == ConsumerState::Failed && budget.used() == queue_reservation + storage_reservation);
    }
    CHECK(budget.used() == 0); // Failed queue no longer pins its raw block.
    auto after = hub.addConsumer(ConsumerKind::Recording, {64, 1}); CHECK(after);
}
void mixedSignalTypes() {
    MemoryBudget budget(65536);
    auto config = firstStream();
    config.signal_type = "aux";
    config.channels[0].descriptor.signal_type = "voltage";
    config.channels[1].descriptor.signal_type = "temperature";
    config.channels[1].descriptor.unit = Unit::Celsius;
    InProcessSimulator sim({config}, budget);
    AcquisitionSession session(sim.deviceDescriptor(), SessionId{"mixed"}, 1);
    const auto prepared = session.prepare(sim.descriptors()); CHECK(prepared);
    const auto capture = *prepared.value;
    CHECK(capture.streams[0].signal_type == "aux" && capture.streams[0].channels[0].signal_type == "voltage");
    CHECK(capture.streams[0].channels[1].signal_type == "temperature");
    auto mismatch = capture;
    mismatch.streams[0].signal_type = "voltage";
    CHECK(sim.prepare(mismatch));
    CHECK(!sim.prepare(capture) && !sim.start(capture.capture_id));
    CHECK(sim.step().blocks.size() == 1);
}
std::vector<std::vector<float>> icaSamples(int rate, int blockSize) {
    MemoryBudget budget(4 * 1024 * 1024);
    auto config = icaSimulatorStream(rate); config.samples_per_step = blockSize;
    InProcessSimulator simulator({config}, budget);
    AcquisitionSession session(simulator.deviceDescriptor(), SessionId{"ica-test"}, 1);
    auto prepared = session.prepare(simulator.descriptors()); CHECK(prepared);
    CHECK(!simulator.prepare(*prepared.value) && !simulator.start(prepared.value->capture_id));
    const auto& descriptor = prepared.value->streams[0];
    CHECK(descriptor.channels.size() == 5 && descriptor.dtype == DType::Float32 && descriptor.clock.simulated);
    CHECK(descriptor.channels[3].signal_type == "eog" && descriptor.channels[4].signal_type == "emg");
    std::vector<std::vector<float>> samples(5);
    for (int first = 0; first < rate * 8; first += blockSize) {
        auto step = simulator.step(); CHECK(!step.error && step.blocks.size() == 1);
        const auto& block = step.blocks[0]; CHECK(block.first_sample_index == std::uint64_t(first));
        const auto& values = *block.storage->data<float>();
        for (int ch = 0; ch < 5; ++ch)
            samples[ch].insert(samples[ch].end(), values.begin() + ch * blockSize, values.begin() + (ch + 1) * blockSize);
    }
    return samples;
}
void icaMixture() {
    const auto baseline = icaSamples(250, 5);
    CHECK(baseline == icaSamples(250, 10));
    for (int rate : {500, 1000}) {
        const auto highRate = icaSamples(rate, rate / 50);
        for (int ch = 0; ch < 5; ++ch)
            for (int i = 0; i < 2000; ++i) CHECK(baseline[ch][i] == highRate[ch][i * (rate / 250)]);
    }
    for (int i = 0; i < 2000; ++i) {
        const double brain = baseline[0][i] - .5 * baseline[3][i] - .3 * baseline[4][i];
        CHECK(std::abs(baseline[1][i] - (.3 * brain + 1.2 * baseline[3][i] + .1 * baseline[4][i])) < 2e-5);
        CHECK(std::abs(baseline[2][i] - (brain - .2 * baseline[3][i] + 1.1 * baseline[4][i])) < 2e-5);
    }
    CHECK(*std::max_element(baseline[3].begin(), baseline[3].end()) > 84);
    CHECK(*std::max_element(baseline[4].begin(), baseline[4].end()) > 15);
    auto config = icaSimulatorStream(250); config.channels[0].mixing[0] = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try { MemoryBudget budget(65536); InProcessSimulator bad({config}, budget); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}
} // namespace
int main() {
    try {
        waveformGapAndFanout(); memoryPressureAndTerminalGap(); clockAndCycles();
        midstreamStartAndUnknownGap();
        for (int i = 0; i < 5; ++i) subscriptionAndStopRaces();
        abandonedConsumerReleasesStorage(); mixedSignalTypes(); icaMixture();
        std::cout << "acquisition_simulator_test: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "acquisition_simulator_test: " << e.what() << '\n';
        return 1;
    }
}
