#include "controller/acquisition_format.h"

#include <QJsonArray>
#include <QtEndian>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace bf::acquisition;
using namespace bf::acquisition_io;

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("check failed at line ") + std::to_string(__LINE__) + ": " #x); } while (false)

namespace {
DeviceDescriptor device()
{
    DeviceDescriptor d;
    d.device_id = DeviceId{"device-1"};
    d.boot_id = BootId{"33333333-3333-4333-8333-333333333333"};
    return d;
}

StreamDescriptor descriptor(DType type = DType::Int24)
{
    StreamDescriptor d;
    d.key = {SessionId{"11111111-1111-4111-8111-111111111111"}, 17};
    d.capture_id = CaptureId{"22222222-2222-4222-8222-222222222222"};
    d.generation = UINT64_C(9007199254740993);
    d.revision = 9;
    d.name = "synthetic";
    d.signal_type = "eeg";
    d.rate = {1000, 1};
    d.dtype = type;
    d.clock = {0, UINT64_C(9007199254740993), 1, ClockSource::Hardware, ClockEpoch::BootRelative, false};
    d.sample_index_source = SampleIndexSource::AdapterReceived;
    d.max_samples_per_block = 64;
    ChannelDescriptor a;
    a.channel_id = "ch1"; a.name = "first"; a.signal_type = "eeg"; a.unit = Unit::Microvolt;
    a.scale = 0.125; a.offset = -2.0;
    a.valid_adc_bits = type == DType::Int24 ? 24 : 16;
    a.minimum = -10; a.maximum = 10;
    ChannelDescriptor b = a;
    b.channel_id = "ch2"; b.name = "second";
    d.channels = {a, b};
    return d;
}

void setLE(QByteArray& bytes, int offset, std::uint64_t value, int width)
{
    for (int i = 0; i < width; ++i) bytes[offset + i] = static_cast<char>((value >> (i * 8)) & 0xff);
}

QByteArray payload(const StreamDescriptor& d, const QByteArray& samples, std::uint32_t count)
{
    QByteArray result(48, '\0');
    setLE(result, 0, d.revision, 4);
    result[4] = static_cast<char>(static_cast<int>(d.dtype) + 1);
    setLE(result, 6, 1, 2);
    setLE(result, 8, d.channels.size(), 4);
    setLE(result, 12, count, 4);
    setLE(result, 16, UINT64_C(9007199254740993), 8);
    setLE(result, 24, UINT64_C(9007199254741003), 8);
    setLE(result, 32, d.clock.clock_id, 4);
    result.append(samples);
    return result;
}

void jsonRoundTrip()
{
    const auto d = descriptor();
    const auto identity = identityToJson(device(), d);
    CHECK(identity.value("device_id").toString() == "device-1");
    CHECK(identity.value("boot_id").toString() == "33333333-3333-4333-8333-333333333333");
    CHECK(identity.value("session_id").toString() == "11111111-1111-4111-8111-111111111111");
    CHECK(identity.value("capture_id").toString() == "22222222-2222-4222-8222-222222222222");
    CHECK(identity.value("stream_id").toInt() == 17 && identity.value("descriptor_revision").toInt() == 9);
    CHECK(identity.value("generation").toString() == "9007199254740993");
    const auto json = descriptorToJson(device(), d, UINT64_C(9007199254740997));
    CHECK(json.value("record_start_sample_index").toString() == "9007199254740997");
    CHECK(json.value("clock").toObject().value("ticks_per_second_num").toString() == "9007199254740993");
    CHECK(json.value("channels").toArray()[0].toObject().value("adc_bits").toInt() == 24);
    auto decoded = descriptorFromJson(json, d.generation);
    CHECK(decoded);
    CHECK(decoded.value->generation == d.generation);
    CHECK(decoded.value->clock.clock_id == 0 && decoded.value->clock.ticks_per_second_num == d.clock.ticks_per_second_num);
    CHECK(decoded.value->channels[0].unit == Unit::Microvolt && decoded.value->channels[0].scale == 0.125);
    CHECK(decoded.value->channels[0].offset == -2.0 && decoded.value->channels[0].valid_adc_bits == 24);
    CHECK(decoded.value->channels[0].minimum == -10 && decoded.value->channels[0].maximum == 10);
    CHECK(decoded.value->sample_index_source == SampleIndexSource::AdapterReceived);
    CHECK(!descriptorToJson(device(), d).contains("record_start_sample_index"));
    auto local = d;
    local.max_samples_per_block = 174762; // 1 MiB raw limit, too large with a BFSP prefix.
    const auto localJson = descriptorToJson(device(), local);
    CHECK(!localJson.isEmpty() && localJson.value("max_samples_per_block").toInt() == 174762);
    CHECK(!descriptorFromJson(localJson, d.generation));
    CHECK(!descriptorFromJson(json, d.generation + 1));
    auto wire = json;
    wire.remove("generation");
    CHECK(descriptorFromJson(wire, d.generation));
}

void jsonValidation()
{
    const auto d = descriptor();
    auto json = descriptorToJson(device(), d);
    auto channels = json.value("channels").toArray();
    auto channel = channels[0].toObject();
    channel["unit"] = QJsonValue(QJsonValue::Null); channel["scale"] = 1.0; channel["offset"] = 0;
    channels[0] = channel; json["channels"] = channels;
    auto unknown = descriptorFromJson(json, d.generation);
    CHECK(unknown && !unknown.value->channels[0].unit);
    channel["unit"] = "mystery"; channels[0] = channel; json["channels"] = channels;
    CHECK(!descriptorFromJson(json, d.generation));
    channel["unit"] = QJsonValue(QJsonValue::Null); channel["scale"] = 0.5; channels[0] = channel; json["channels"] = channels;
    CHECK(!descriptorFromJson(json, d.generation));

    json = descriptorToJson(device(), d); channels = json.value("channels").toArray(); channel = channels[0].toObject();
    channel["adc_bits"] = 25; channels[0] = channel; json["channels"] = channels;
    CHECK(!descriptorFromJson(json, d.generation));
    channel["adc_bits"] = "24"; channels[0] = channel; json["channels"] = channels;
    CHECK(!descriptorFromJson(json, d.generation));
    channel["adc_bits"] = 24; channel["valid_adc_bits"] = 23; channels[0] = channel; json["channels"] = channels;
    CHECK(!descriptorFromJson(json, d.generation));
    channel.remove("adc_bits"); channel["valid_adc_bits"] = 24; channels[0] = channel; json["channels"] = channels;
    CHECK(descriptorFromJson(json, d.generation));
    channel["minimum"] = 11; channels[0] = channel; json["channels"] = channels;
    CHECK(!descriptorFromJson(json, d.generation));
    channel["minimum"] = std::numeric_limits<double>::quiet_NaN(); channels[0] = channel; json["channels"] = channels;
    CHECK(!descriptorFromJson(json, d.generation));
    json = descriptorToJson(device(), d);
    auto clock = json.value("clock").toObject();
    clock["clock_id"] = -1; json["clock"] = clock;
    CHECK(!descriptorFromJson(json, d.generation));
    clock["clock_id"] = 0; clock["ticks_per_second_num"] = "90071992547409930x"; json["clock"] = clock;
    CHECK(!descriptorFromJson(json, d.generation));
    clock["ticks_per_second_num"] = "9007199254740993"; clock["simulated"] = "false"; json["clock"] = clock;
    CHECK(!descriptorFromJson(json, d.generation));
    json = descriptorToJson(device(), d); json["generation"] = 42;
    CHECK(!descriptorFromJson(json, d.generation));
    json = descriptorToJson(device(), d); json["record_start_sample_index"] = 42;
    CHECK(!descriptorFromJson(json, d.generation));
    auto invalid = d;
    invalid.channels[0].unit = Unit::Pascal;
    CHECK(descriptorToJson(device(), invalid).isEmpty());
}

void mixedAuxStreamRoundTrip()
{
    const auto d = descriptor();
    auto wire = descriptorToJson(device(), d);
    wire["signal_type"] = "aux";
    auto channels = wire.value("channels").toArray();
    auto voltage = channels[0].toObject();
    voltage["signal_type"] = "voltage"; voltage["unit"] = "mV"; voltage["scale"] = 0.125;
    auto temperature = channels[1].toObject();
    temperature["signal_type"] = "temperature"; temperature["unit"] = "degC";
    temperature["scale"] = 0.25; temperature["offset"] = 18.0;
    channels[0] = voltage; channels[1] = temperature; wire["channels"] = channels;
    auto decoded = descriptorFromJson(wire, d.generation);
    CHECK(decoded && decoded.value->signal_type == "aux");
    CHECK(decoded.value->channels[0].signal_type == "voltage" && decoded.value->channels[0].unit == Unit::Millivolt);
    CHECK(decoded.value->channels[1].signal_type == "temperature" && decoded.value->channels[1].unit == Unit::Celsius);
    CHECK(decoded.value->channels[1].scale == 0.25 && decoded.value->channels[1].offset == 18.0);
    const auto encoded = descriptorToJson(device(), *decoded.value);
    CHECK(encoded.value("signal_type").toString() == "aux");
    const auto encodedChannels = encoded.value("channels").toArray();
    CHECK(encodedChannels[0].toObject().value("signal_type").toString() == "voltage");
    CHECK(encodedChannels[0].toObject().value("unit").toString() == "mV");
    CHECK(encodedChannels[1].toObject().value("signal_type").toString() == "temperature");
    CHECK(encodedChannels[1].toObject().value("unit").toString() == "degC");
    CHECK(encodedChannels[1].toObject().value("scale").toDouble() == 0.25);
    CHECK(encodedChannels[1].toObject().value("offset").toDouble() == 18.0);
}

void rawInt24()
{
    const auto d = descriptor();
    const QByteArray raw = QByteArray::fromHex("000080ffffff000000ffff7f"); // two channels, two samples each
    const auto data = payload(d, raw, 2);
    MemoryBudget budget(1024);
    auto decoded = blockFromPayload(d, data, budget, UINT64_C(9007199254741005), "host-clock");
    if (!decoded && decoded.error) {
        std::cerr << decoded.error->code;
        for (const auto& entry : decoded.error->params) std::cerr << ' ' << entry.first << '=' << entry.second;
        std::cerr << '\n';
    }
    CHECK(decoded && budget.used() == decoded.value->storage->accountedBytes());
    const auto& b = *decoded.value;
    CHECK(b.stream == d.key && b.capture_id == d.capture_id && b.generation == d.generation);
    CHECK(b.first_sample_index == UINT64_C(9007199254740993));
    CHECK(b.first_timestamp_ticks == UINT64_C(9007199254741003));
    CHECK(b.host_received_monotonic_ns == UINT64_C(9007199254741005) && b.host_clock_id == "host-clock");
    const auto* values = b.storage->data<std::int32_t>();
    CHECK(values && *values == std::vector<std::int32_t>({-8388608, -1, 0, 8388607}));
    const auto encoded = encodeRawSamples(b);
    CHECK(encoded && *encoded.value == raw);
    decoded.value.reset();
    CHECK(budget.used() == 0);
}

void rawOtherTypes()
{
    struct Example { DType type; const char* hex; };
    const Example cases[] = {
        {DType::Int16, "0080ff7f0000ffff"},
        {DType::Int32, "00000080ffffff7f00000000ffffffff"},
        {DType::Float32, "000000800100803f0000000080000000"},
        {DType::Float64, "0000000000000080010000000000f03f00000000000000000000000000000080"},
    };
    for (const auto& example : cases) {
        auto d = descriptor(example.type);
        const auto raw = QByteArray::fromHex(example.hex);
        MemoryBudget budget(1024);
        auto decoded = blockFromPayload(d, payload(d, raw, 2), budget, std::nullopt, {});
        CHECK(decoded && !decoded.value->host_received_monotonic_ns);
        CHECK(decoded.value->storage->dtype() == example.type);
        const auto encoded = encodeRawSamples(*decoded.value);
        CHECK(encoded && *encoded.value == raw);
        if (example.type == DType::Float64) {
            const auto* values = decoded.value->storage->data<double>();
            CHECK(values && std::signbit((*values)[0]));
            CHECK((*values)[1] == std::nextafter(1.0, 2.0) && (*values)[1] != 1.0);
        }
    }
}

void rejectionAndBudget()
{
    auto d = descriptor();
    const QByteArray raw = QByteArray::fromHex("000080ffffff000000ffff7f");
    const auto good = payload(d, raw, 2);
    MemoryBudget tiny(15);
    const auto noMemory = blockFromPayload(d, good, tiny, 0, "host");
    CHECK(!noMemory && noMemory.error->code == "buffer.budget_exhausted" && tiny.used() == 0);
    MemoryBudget budget(1024);
    auto bad = good;
    setLE(bad, 0, d.revision + 1, 4);
    auto mismatch = blockFromPayload(d, bad, budget, 1, "host");
    CHECK(!mismatch && mismatch.error->code == "acquisition.invalid_block");
    bad = good; setLE(bad, 8, 1, 4); setLE(bad, 12, 4, 4);
    mismatch = blockFromPayload(d, bad, budget, 1, "host");
    CHECK(!mismatch && mismatch.error->code == "acquisition.invalid_block");
    bad = good; setLE(bad, 32, 1, 4);
    mismatch = blockFromPayload(d, bad, budget, 1, "host");
    CHECK(!mismatch && mismatch.error->code == "acquisition.invalid_block");
    bad = good; bad[4] = 1; setLE(bad, 12, 3, 4);
    mismatch = blockFromPayload(d, bad, budget, 1, "host");
    CHECK(!mismatch && mismatch.error->code == "acquisition.invalid_block");
    bad = good; setLE(bad, 6, 3, 2);
    CHECK(!blockFromPayload(d, bad, budget, 1, "host"));
    auto estimated = d; estimated.clock.source = ClockSource::Estimated;
    CHECK(!blockFromPayload(estimated, good, budget, 1, "host"));
    bad = good; setLE(bad, 6, 3, 2);
    CHECK(blockFromPayload(estimated, bad, budget, 1, "host"));
    bad = good; bad.chop(1);
    CHECK(!blockFromPayload(d, bad, budget, 1, "host"));
    CHECK(!blockFromPayload(d, good, budget, 1, {}));
    CHECK(budget.used() == 0);
    auto block = blockFromPayload(d, good, budget, std::nullopt, {});
    CHECK(block);
    auto malformed = *block.value;
    malformed.channel_count = 1;
    CHECK(!encodeRawSamples(malformed));
    malformed.storage.reset();
    CHECK(!encodeRawSamples(malformed));
}
} // namespace

int main()
{
    try {
        jsonRoundTrip(); jsonValidation(); mixedAuxStreamRoundTrip(); rawInt24(); rawOtherTypes(); rejectionAndBudget();
        std::cout << "acquisition_format_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "acquisition_format_test: " << error.what() << '\n';
        return 1;
    }
}
