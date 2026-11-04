#include "acquisition/model/signal_model.h"
#include "acquisition/session/acquisition_session.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

using namespace bf::acquisition;

static_assert(!std::is_copy_constructible_v<SampleStorage>, "Copying sample storage must not bypass the budget");
static_assert(!std::is_move_constructible_v<SampleStorage>, "Published storage must stay immutable");
static_assert(!std::is_copy_constructible_v<AcquisitionSession>, "A session cannot fork its stream identity counter");

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("check failed at line ") + std::to_string(__LINE__) + ": " #x); } while (false)

namespace {
StreamDescriptor descriptor() {
    StreamDescriptor d;
    d.key = {SessionId{"session"}, 1};
    d.capture_id = CaptureId{"capture"};
    d.generation = 9;
    d.signal_type = "eeg";
    d.rate = {1000, 1};
    d.dtype = DType::Int24;
    d.clock = {0, 1000000000, 1, ClockSource::Estimated, ClockEpoch::SessionRelative, false};
    d.max_samples_per_block = 4;
    ChannelDescriptor ch;
    ch.channel_id = "ch1";
    ch.name = "EEG";
    ch.signal_type = "eeg";
    ch.unit = Unit::Microvolt;
    ch.scale = 0.125;
    ch.offset = -2;
    ch.valid_adc_bits = 24;
    d.channels.push_back(ch);
    return d;
}
SampleBlock block(const StreamDescriptor& d, const MemoryBudget& budget) {
    SampleBlock b;
    b.stream = d.key; b.capture_id = d.capture_id; b.generation = d.generation;
    b.descriptor_revision = d.revision; b.channel_count = 1; b.sample_count = 4;
    b.first_timestamp_ticks = 0; b.clock_id = 0; b.timestamp_estimated = true;
    b.storage = SampleStorage::fromInt32(DType::Int24, {-8388608, -1, 0, 8388607}, budget);
    return b;
}
void modelInvariants() {
    MemoryBudget budget(4096);
    std::vector<std::int32_t> input{-8388608, -1, 0, 8388607};
    auto storage = SampleStorage::fromInt32(DType::Int24, input, budget);
    CHECK(storage && budget.used() == storage->accountedBytes() && storage->bytes() == 16);
    const auto charged = storage->accountedBytes();
    CHECK(charged > storage->bytes());
    input[0] = 123;
    CHECK(storage->data<std::int32_t>()->at(0) == -8388608);
    CHECK(storage->data<std::int32_t>()->at(3) == 8388607);
    CHECK(storage->dtype() == DType::Int24 && !storage->data<float>());
    auto shared = storage;
    storage.reset();
    CHECK(budget.used() == charged);
    shared.reset();
    CHECK(budget.used() == 0);

    auto a = SampleStorage::fromInt16({-32768, 32767}, budget);
    auto b = SampleStorage::fromInt32(DType::Int32, {std::numeric_limits<std::int32_t>::min(),
                                                      std::numeric_limits<std::int32_t>::max()}, budget);
    auto c = SampleStorage::fromFloat32({0.25f, -1.5f}, budget);
    auto d = SampleStorage::fromFloat64({1.0 + std::ldexp(1.0, -40)}, budget);
    CHECK(a && b && c && d && budget.used() == a->accountedBytes() + b->accountedBytes() + c->accountedBytes() + d->accountedBytes());
    CHECK(d->data<double>()->at(0) == 1.0 + std::ldexp(1.0, -40));
    CHECK(c->data<float>()->at(1) == -1.5f);
    bool bad = false;
    try { SampleStorage::fromInt32(DType::Int24, {8388608}, budget); } catch (const std::invalid_argument&) { bad = true; }
    CHECK(bad);
    bad = false;
    try { SampleStorage::fromFloat64({std::numeric_limits<double>::infinity()}, budget); } catch (const std::invalid_argument&) { bad = true; }
    CHECK(bad && budget.used() == a->accountedBytes() + b->accountedBytes() + c->accountedBytes() + d->accountedBytes());
    MemoryBudget tiny(3);
    CHECK(!SampleStorage::fromInt16({1, 2}, tiny));
    CHECK(tiny.used() == 0 && !a->belongsTo(tiny) && a->belongsTo(budget));

    auto desc = descriptor();
    CHECK(!validateStreamDescriptor(desc)); // The protocol permits clock_id == 0.
    CHECK(physicalValue(16.0, desc.channels[0]) == 0.0);
    auto sample = block(desc, budget);
    CHECK(!validateSampleBlock(desc, sample));
    sample.first_sample_index = std::numeric_limits<std::uint64_t>::max() - 4;
    CHECK(!validateSampleBlock(desc, sample));
    ++sample.first_sample_index;
    CHECK(validateSampleBlock(desc, sample));
    sample.first_sample_index = 0;
    sample.clock_id = 1;
    CHECK(validateSampleBlock(desc, sample));
    sample.clock_id = 0;
    sample.host_received_monotonic_ns = 0;
    CHECK(validateSampleBlock(desc, sample));
    sample.host_clock_id = "host-monotonic";
    CHECK(!validateSampleBlock(desc, sample));
    sample.timestamp_estimated = false;
    CHECK(validateSampleBlock(desc, sample));
    auto invalid = desc;
    invalid.channels[0].unit.reset();
    CHECK(validateStreamDescriptor(invalid)); // Unknown unit cannot pretend to be calibrated.
    invalid.channels[0].scale = 1; invalid.channels[0].offset = 0;
    CHECK(!validateStreamDescriptor(invalid));
    invalid.channels.push_back(invalid.channels[0]);
    CHECK(validateStreamDescriptor(invalid));
    invalid = desc;
    invalid.clock.source = static_cast<ClockSource>(99);
    CHECK(validateStreamDescriptor(invalid));
    invalid = desc;
    invalid.channels[0].unit = static_cast<Unit>(99);
    CHECK(validateStreamDescriptor(invalid));
    invalid = desc;
    invalid.signal_type.clear();
    CHECK(validateStreamDescriptor(invalid));
    auto mixed = desc;
    mixed.signal_type = "aux";
    mixed.channels[0].signal_type = "voltage";
    auto temperature = mixed.channels[0];
    temperature.channel_id = "temperature";
    temperature.signal_type = "temperature";
    temperature.unit = Unit::Celsius;
    mixed.channels.push_back(temperature);
    CHECK(!validateStreamDescriptor(mixed)); // Stream type need not match channel types.
}
void identityAndLifecycle() {
    auto device = DeviceDescriptor{};
    device.device_id = DeviceId{"device"}; device.boot_id = BootId{"boot"};
    AcquisitionSession session(device, SessionId{"session-a"}, 100);
    auto templ = descriptor();
    auto prepared = session.prepare({templ});
    CHECK(prepared && prepared.value->streams[0].key.stream_id == 1);
    auto first = *prepared.value;
    CHECK(!session.prepare({templ}));
    CHECK(!session.start(first.capture_id));
    CHECK(!session.start(first.capture_id));
    MemoryBudget budget(1024);
    auto sample = block(first.streams[0], budget);
    CHECK(session.accepts(sample));
    auto foreign = sample;
    foreign.generation++;
    CHECK(!session.accepts(foreign));
    CHECK(!session.stop(first.capture_id));
    CHECK(!session.stop(first.capture_id));
    CHECK(!session.accepts(sample));
    auto next = session.prepare({templ});
    CHECK(next && next.value->capture_id != first.capture_id && next.value->streams[0].key.stream_id == 2);
    CHECK(session.start(first.capture_id));
    CHECK(!session.start(next.value->capture_id));
    session.invalidate();
    CHECK(session.generation() == 101 && session.state() == SessionState::Idle && !session.accepts(sample));
    auto third = session.prepare({templ});
    CHECK(third && third.value->generation == 101 && third.value->streams[0].key.stream_id == 3);
    const auto id = newIdentity();
    CHECK(id.size() == 36 && id[14] == '4' && (id[19] == '8' || id[19] == '9' || id[19] == 'a' || id[19] == 'b'));
}
void concurrentBudget() {
    MemoryBudget budget(640);
    std::atomic<bool> go{false}, release{false}, invalid{false};
    std::atomic<int> arrived{0}, success{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
        while (!go.load()) std::this_thread::yield();
        auto memory = SampleStorage::fromInt32(DType::Int32, std::vector<std::int32_t>(32), budget);
        if (memory) ++success;
        if (budget.used() > budget.limit()) invalid = true;
        ++arrived;
        while (!release.load()) std::this_thread::yield();
    });
    go = true;
    while (arrived.load() != 8) std::this_thread::yield();
    const bool okay = success == 1 && budget.used() == budget.limit() && !invalid;
    release = true;
    for (auto& worker : workers) worker.join();
    CHECK(okay && budget.used() == 0);
}
} // namespace
int main() {
    try {
        modelInvariants(); identityAndLifecycle(); concurrentBudget();
        std::cout << "signal_model_test: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "signal_model_test: " << e.what() << '\n';
        return 1;
    }
}
