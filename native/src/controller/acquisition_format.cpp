#include "controller/acquisition_format.h"

#include "acquisition/protocol/codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bf::acquisition_io {
namespace {
using namespace acquisition;

template <typename T>
Result<T> failure(const char* code, const std::string& field)
{
    return {std::nullopt, DeviceError{code, {{"field", field}}, std::nullopt, false, "format"}};
}

template <typename T>
Result<T> failure(const DeviceError& error)
{
    return {std::nullopt, error};
}

template <typename T>
Result<T> protocolFailure(const bfsp::Result& error)
{
    return failure<T>(bfsp::error_code(error.error), error.detail.toStdString());
}

QString asQString(const std::string& value) { return QString::fromStdString(value); }
QString decimal(std::uint64_t value) { return QString::number(value); }

std::optional<DType> parseDType(const QString& value)
{
    if (value == "int16") return DType::Int16;
    if (value == "int24") return DType::Int24;
    if (value == "int32") return DType::Int32;
    if (value == "float32") return DType::Float32;
    if (value == "float64") return DType::Float64;
    return std::nullopt;
}

std::optional<Unit> parseUnit(const QString& value)
{
    if (value == "V") return Unit::Volt;
    if (value == "mV") return Unit::Millivolt;
    if (value == "uV") return Unit::Microvolt;
    if (value == "A") return Unit::Ampere;
    if (value == "mA") return Unit::Milliampere;
    if (value == "Pa") return Unit::Pascal;
    if (value == "kPa") return Unit::Kilopascal;
    if (value == "degC") return Unit::Celsius;
    if (value == "1") return Unit::Dimensionless;
    if (value == "count") return Unit::Count;
    return std::nullopt;
}

bool unsigned32(const QJsonValue& value, std::uint32_t& output, bool positive = false)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < (positive ? 1.0 : 0.0) ||
        number > std::numeric_limits<std::uint32_t>::max() || std::floor(number) != number) return false;
    output = static_cast<std::uint32_t>(number);
    return true;
}

bool unsigned64String(const QJsonValue& value, std::uint64_t& output)
{
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text[0] == QLatin1Char('0'))) return false;
    std::uint64_t number = 0;
    for (const auto c : text) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
        const auto digit = static_cast<unsigned>(c.unicode() - '0');
        if (number > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        number = number * 10 + digit;
    }
    output = number;
    return true;
}

bool optionalNumber(const QJsonObject& object, const char* key, std::optional<double>& result)
{
    const auto value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) return true;
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
    result = value.toDouble();
    return true;
}

bool optionalAdcBits(const QJsonObject& object, const char* key, unsigned maxBits,
                     std::optional<std::uint8_t>& result)
{
    const auto value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) return true;
    std::uint32_t number = 0;
    if (!unsigned32(value, number, true) || number > maxBits) return false;
    if (result && *result != number) return false;
    result = static_cast<std::uint8_t>(number);
    return true;
}

bool finiteJson(const QJsonValue& value)
{
    if (value.isDouble()) return std::isfinite(value.toDouble());
    if (value.isArray()) {
        for (const auto& child : value.toArray()) if (!finiteJson(child)) return false;
    } else if (value.isObject()) {
        const auto object = value.toObject();
        for (auto i = object.begin(); i != object.end(); ++i) if (!finiteJson(i.value())) return false;
    }
    return true;
}

std::optional<bfsp::DType> wireDType(DType type)
{
    switch (type) {
    case DType::Int16: return bfsp::DType::Int16;
    case DType::Int24: return bfsp::DType::Int24;
    case DType::Int32: return bfsp::DType::Int32;
    case DType::Float32: return bfsp::DType::Float32;
    case DType::Float64: return bfsp::DType::Float64;
    }
    return std::nullopt;
}

int wireWidth(DType type)
{
    switch (type) {
    case DType::Int16: return 2;
    case DType::Int24: return 3;
    case DType::Int32: case DType::Float32: return 4;
    case DType::Float64: return 8;
    }
    return 0;
}

std::uint32_t unsigned24(const uchar* p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16);
}

std::int32_t signedBits(std::uint32_t value, unsigned bits)
{
    const auto sign = std::uint64_t{1} << (bits - 1);
    const auto wide = static_cast<std::uint64_t>(value);
    return static_cast<std::int32_t>(wide & sign ? static_cast<std::int64_t>(wide) - static_cast<std::int64_t>(sign * 2)
                                                 : static_cast<std::int64_t>(wide));
}
} // namespace

QString dtypeName(DType dtype)
{
    switch (dtype) {
    case DType::Int16: return "int16";
    case DType::Int24: return "int24";
    case DType::Int32: return "int32";
    case DType::Float32: return "float32";
    case DType::Float64: return "float64";
    }
    return {};
}

QString unitName(Unit unit)
{
    switch (unit) {
    case Unit::Volt: return "V";
    case Unit::Millivolt: return "mV";
    case Unit::Microvolt: return "uV";
    case Unit::Ampere: return "A";
    case Unit::Milliampere: return "mA";
    case Unit::Pascal: return "Pa";
    case Unit::Kilopascal: return "kPa";
    case Unit::Celsius: return "degC";
    case Unit::Dimensionless: return "1";
    case Unit::Count: return "count";
    }
    return {};
}

QJsonObject identityToJson(const DeviceDescriptor& device, const StreamDescriptor& stream)
{
    return {{"device_id", asQString(device.device_id.value)}, {"boot_id", asQString(device.boot_id.value)},
            {"session_id", asQString(stream.key.session_id.value)}, {"capture_id", asQString(stream.capture_id.value)},
            {"generation", decimal(stream.generation)}, {"stream_id", static_cast<double>(stream.key.stream_id)},
            {"descriptor_revision", static_cast<double>(stream.revision)}};
}

QJsonObject descriptorToJson(const DeviceDescriptor& device, const StreamDescriptor& stream,
                             std::optional<std::uint64_t> recordStart)
{
    if (validateStreamDescriptor(stream)) return {};
    auto result = identityToJson(device, stream);
    result.insert("name", asQString(stream.name));
    result.insert("signal_type", asQString(stream.signal_type));
    result.insert("rate_num", static_cast<double>(stream.rate.numerator));
    result.insert("rate_den", static_cast<double>(stream.rate.denominator));
    result.insert("dtype", dtypeName(stream.dtype));
    result.insert("layout", "channel_major");
    result.insert("max_samples_per_block", static_cast<double>(stream.max_samples_per_block));
    result.insert("sample_index_source", stream.sample_index_source == SampleIndexSource::Device ? "device" : "adapter_received");
    if (recordStart) result.insert("record_start_sample_index", decimal(*recordStart));
    QJsonArray channels;
    for (const auto& channel : stream.channels) {
        QJsonObject value{{"channel_id", asQString(channel.channel_id)}, {"name", asQString(channel.name)},
                          {"signal_type", asQString(channel.signal_type)}, {"scale", channel.scale}, {"offset", channel.offset}};
        value.insert("unit", channel.unit ? QJsonValue(unitName(*channel.unit)) : QJsonValue(QJsonValue::Null));
        if (channel.valid_adc_bits) value.insert("adc_bits", *channel.valid_adc_bits);
        if (channel.minimum) value.insert("minimum", *channel.minimum);
        if (channel.maximum) value.insert("maximum", *channel.maximum);
        channels.append(value);
    }
    result.insert("channels", channels);
    const auto& clock = stream.clock;
    const QString source = clock.source == ClockSource::Hardware ? "hardware" :
                           clock.source == ClockSource::HostMonotonic ? "host_monotonic" : "estimated";
    result.insert("clock", QJsonObject{{"clock_id", static_cast<double>(clock.clock_id)},
        {"ticks_per_second_num", decimal(clock.ticks_per_second_num)},
        {"ticks_per_second_den", decimal(clock.ticks_per_second_den)}, {"source", source},
        {"epoch", clock.epoch == ClockEpoch::BootRelative ? "boot_relative" : "session_relative"},
        {"simulated", clock.simulated}});
    // capture.v1 can accept a full 1 MiB of raw samples. BFSP DATA instead
    // reserves 48 bytes of its limit for a prefix, so validate the same
    // metadata against a legal wire block size without changing the result.
    auto validation = result;
    const auto wireMaxSamples = (bfsp::kMaxPayload - bfsp::kDataPrefixBytes) /
            (stream.channels.size() * static_cast<std::size_t>(wireWidth(stream.dtype)));
    if (stream.max_samples_per_block > wireMaxSamples)
        validation.insert("max_samples_per_block", static_cast<double>(wireMaxSamples));
    const auto bytes = QJsonDocument(validation).toJson(QJsonDocument::Compact);
    if (!bfsp::validate_json(bfsp::MessageType::Descriptor, stream.key.stream_id, QByteArrayView(bytes))) return {};
    return result;
}

Result<StreamDescriptor> descriptorFromJson(const QJsonObject& json, Generation generation)
{
    std::uint32_t streamId = 0;
    if (!unsigned32(json.value("stream_id"), streamId, true))
        return failure<StreamDescriptor>("bfsp.invalid_payload", "stream_id");
    const auto bytes = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QJsonObject parsed;
    const auto checked = bfsp::validate_json(bfsp::MessageType::Descriptor, streamId, QByteArrayView(bytes), &parsed);
    if (!checked) return protocolFailure<StreamDescriptor>(checked);
    if (!finiteJson(json)) return failure<StreamDescriptor>("bfsp.invalid_payload", "nonfinite_json");
    if (parsed.contains("generation")) {
        std::uint64_t local = 0;
        if (!unsigned64String(parsed.value("generation"), local) || local != generation)
            return failure<StreamDescriptor>("acquisition.invalid_descriptor", "generation");
    }
    if (parsed.contains("record_start_sample_index")) {
        std::uint64_t start = 0;
        if (!unsigned64String(parsed.value("record_start_sample_index"), start))
            return failure<StreamDescriptor>("acquisition.invalid_descriptor", "record_start_sample_index");
    }
    StreamDescriptor result;
    result.key = {SessionId{parsed.value("session_id").toString().toStdString()}, streamId};
    result.capture_id = CaptureId{parsed.value("capture_id").toString().toStdString()};
    result.generation = generation;
    unsigned32(parsed.value("descriptor_revision"), result.revision, true);
    result.name = parsed.value("name").toString().toStdString();
    result.signal_type = parsed.value("signal_type").toString().toStdString();
    unsigned32(parsed.value("rate_num"), result.rate.numerator, true);
    unsigned32(parsed.value("rate_den"), result.rate.denominator, true);
    result.dtype = *parseDType(parsed.value("dtype").toString());
    unsigned32(parsed.value("max_samples_per_block"), result.max_samples_per_block, true);
    result.sample_index_source = parsed.value("sample_index_source").toString() == "adapter_received"
            ? SampleIndexSource::AdapterReceived : SampleIndexSource::Device;
    const auto clock = parsed.value("clock").toObject();
    unsigned32(clock.value("clock_id"), result.clock.clock_id);
    unsigned64String(clock.value("ticks_per_second_num"), result.clock.ticks_per_second_num);
    unsigned64String(clock.value("ticks_per_second_den"), result.clock.ticks_per_second_den);
    const auto source = clock.value("source").toString();
    result.clock.source = source == "hardware" ? ClockSource::Hardware :
                          source == "host_monotonic" ? ClockSource::HostMonotonic : ClockSource::Estimated;
    result.clock.epoch = clock.value("epoch").toString() == "boot_relative" ? ClockEpoch::BootRelative : ClockEpoch::SessionRelative;
    if (clock.contains("simulated") && !clock.value("simulated").isBool())
        return failure<StreamDescriptor>("acquisition.invalid_descriptor", "clock.simulated");
    result.clock.simulated = clock.value("simulated").toBool(false);
    const auto channels = parsed.value("channels").toArray();
    result.channels.reserve(static_cast<std::size_t>(channels.size()));
    const unsigned maxBits = result.dtype == DType::Int24 ? 24 : static_cast<unsigned>(inMemoryBytesPerSample(result.dtype) * 8);
    for (const auto& element : channels) {
        const auto value = element.toObject();
        ChannelDescriptor channel;
        channel.channel_id = value.value("channel_id").toString().toStdString();
        channel.name = value.value("name").toString().toStdString();
        channel.signal_type = value.value("signal_type").toString().toStdString();
        if (!value.value("unit").isNull()) {
            channel.unit = parseUnit(value.value("unit").toString());
            if (!channel.unit) return failure<StreamDescriptor>("acquisition.invalid_descriptor", "channel.unit");
        }
        channel.scale = value.value("scale").toDouble();
        channel.offset = value.value("offset").toDouble();
        if (!optionalAdcBits(value, "adc_bits", maxBits, channel.valid_adc_bits) ||
            !optionalAdcBits(value, "valid_adc_bits", maxBits, channel.valid_adc_bits))
            return failure<StreamDescriptor>("acquisition.invalid_descriptor", "channel.adc_bits");
        if (!optionalNumber(value, "minimum", channel.minimum) || !optionalNumber(value, "maximum", channel.maximum))
            return failure<StreamDescriptor>("acquisition.invalid_descriptor", "channel.range");
        result.channels.push_back(std::move(channel));
    }
    if (const auto error = validateStreamDescriptor(result)) return failure<StreamDescriptor>(*error);
    return {std::move(result), std::nullopt};
}

Result<SampleBlock> blockFromPayload(const StreamDescriptor& stream, const QByteArray& payload,
                                    const MemoryBudget& budget, std::optional<std::uint64_t> hostNs,
                                    const QString& hostClockId)
{
    if (const auto error = validateStreamDescriptor(stream)) return failure<SampleBlock>(*error);
    bfsp::DataPrefix prefix;
    const auto decoded = bfsp::decode_data(QByteArrayView(payload), prefix);
    if (!decoded) return protocolFailure<SampleBlock>(decoded);
    const auto type = wireDType(stream.dtype);
    if (!type || prefix.descriptor_revision != stream.revision || prefix.dtype != *type ||
        prefix.channel_count != stream.channels.size() || prefix.sample_count > stream.max_samples_per_block ||
        prefix.clock_id != stream.clock.clock_id)
        return failure<SampleBlock>("acquisition.invalid_block", "descriptor_mismatch");
    if (((prefix.time_flags & 2u) && stream.clock.source == ClockSource::Hardware) ||
        ((prefix.time_flags & 1u) && !(prefix.time_flags & 2u) && stream.clock.source == ClockSource::Estimated))
        return failure<SampleBlock>("acquisition.invalid_block", "clock");
    const auto hostId = hostClockId.toStdString();
    if (hostId.size() > 128 || (hostNs && hostId.empty()))
        return failure<SampleBlock>("acquisition.invalid_block", "clock");
    const std::size_t count = static_cast<std::size_t>(prefix.channel_count) * prefix.sample_count;
    const auto bytes = count * inMemoryBytesPerSample(stream.dtype);
    if (bytes > budget.limit() || budget.used() > budget.limit() - bytes)
        return failure<SampleBlock>("buffer.budget_exhausted", "samples");
    SampleBlock block;
    block.stream = stream.key;
    block.capture_id = stream.capture_id;
    block.generation = stream.generation;
    block.descriptor_revision = prefix.descriptor_revision;
    block.channel_count = prefix.channel_count;
    block.sample_count = prefix.sample_count;
    block.first_sample_index = prefix.first_sample_index;
    if (prefix.time_flags & 1u) block.first_timestamp_ticks = prefix.first_timestamp_ticks;
    block.timestamp_estimated = (prefix.time_flags & 2u) != 0;
    block.clock_id = prefix.clock_id;
    block.host_received_monotonic_ns = hostNs;
    block.host_clock_id = hostId;
    block.quality_flags = prefix.quality_flags;
    const auto* p = reinterpret_cast<const uchar*>(payload.constData() + bfsp::kDataPrefixBytes);
    try {
        switch (stream.dtype) {
        case DType::Int16: {
            std::vector<std::int16_t> values(count);
            for (std::size_t i = 0; i < count; ++i, p += 2)
                values[i] = static_cast<std::int16_t>(signedBits(qFromLittleEndian<quint16>(p), 16));
            block.storage = SampleStorage::fromInt16(values, budget);
            break;
        }
        case DType::Int24: case DType::Int32: {
            std::vector<std::int32_t> values(count);
            const int width = wireWidth(stream.dtype);
            for (std::size_t i = 0; i < count; ++i, p += width)
                values[i] = width == 3 ? signedBits(unsigned24(p), 24) : signedBits(qFromLittleEndian<quint32>(p), 32);
            block.storage = SampleStorage::fromInt32(stream.dtype, values, budget);
            break;
        }
        case DType::Float32: {
            std::vector<float> values(count);
            for (std::size_t i = 0; i < count; ++i, p += 4) {
                const auto bits = qFromLittleEndian<quint32>(p);
                std::memcpy(&values[i], &bits, sizeof(bits));
            }
            block.storage = SampleStorage::fromFloat32(values, budget);
            break;
        }
        case DType::Float64: {
            std::vector<double> values(count);
            for (std::size_t i = 0; i < count; ++i, p += 8) {
                const auto bits = qFromLittleEndian<quint64>(p);
                std::memcpy(&values[i], &bits, sizeof(bits));
            }
            block.storage = SampleStorage::fromFloat64(values, budget);
            break;
        }
        }
    } catch (const std::bad_alloc&) {
        return failure<SampleBlock>("buffer.budget_exhausted", "samples");
    } catch (const std::invalid_argument&) {
        return failure<SampleBlock>("acquisition.invalid_block", "samples");
    }
    if (!block.storage) return failure<SampleBlock>("buffer.budget_exhausted", "samples");
    if (const auto error = validateSampleBlock(stream, block)) return failure<SampleBlock>(*error);
    return {std::move(block), std::nullopt};
}

Result<QByteArray> encodeRawSamples(const SampleBlock& block)
{
    if (!block.storage || !block.channel_count || !block.sample_count ||
        block.channel_count > std::numeric_limits<std::size_t>::max() / block.sample_count)
        return failure<QByteArray>("acquisition.invalid_block", "shape");
    const auto count = static_cast<std::size_t>(block.channel_count) * block.sample_count;
    const int width = wireWidth(block.storage->dtype());
    if (!width || count != block.storage->size() || count > bfsp::kMaxPayload / static_cast<std::size_t>(width))
        return failure<QByteArray>("acquisition.invalid_block", "shape_or_dtype");
    try {
        QByteArray output(static_cast<qsizetype>(count * width), Qt::Uninitialized);
        auto* p = reinterpret_cast<uchar*>(output.data());
        switch (block.storage->dtype()) {
        case DType::Int16: {
            const auto* values = block.storage->data<std::int16_t>();
            if (!values) return failure<QByteArray>("acquisition.invalid_block", "dtype");
            for (const auto value : *values) { qToLittleEndian(static_cast<quint16>(value), p); p += 2; }
            break;
        }
        case DType::Int24: case DType::Int32: {
            const auto* values = block.storage->data<std::int32_t>();
            if (!values) return failure<QByteArray>("acquisition.invalid_block", "dtype");
            for (const auto value : *values) {
                const auto bits = static_cast<quint32>(value);
                if (width == 3) { p[0] = bits & 0xff; p[1] = (bits >> 8) & 0xff; p[2] = (bits >> 16) & 0xff; }
                else qToLittleEndian(bits, p);
                p += width;
            }
            break;
        }
        case DType::Float32: {
            const auto* values = block.storage->data<float>();
            if (!values) return failure<QByteArray>("acquisition.invalid_block", "dtype");
            for (const auto value : *values) {
                quint32 bits; std::memcpy(&bits, &value, sizeof(bits)); qToLittleEndian(bits, p); p += 4;
            }
            break;
        }
        case DType::Float64: {
            const auto* values = block.storage->data<double>();
            if (!values) return failure<QByteArray>("acquisition.invalid_block", "dtype");
            for (const auto value : *values) {
                quint64 bits; std::memcpy(&bits, &value, sizeof(bits)); qToLittleEndian(bits, p); p += 8;
            }
            break;
        }
        }
        return {std::move(output), std::nullopt};
    } catch (const std::bad_alloc&) {
        return failure<QByteArray>("buffer.budget_exhausted", "encoding");
    }
}

} // namespace bf::acquisition_io
