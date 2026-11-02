#include "internal.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <cmath>
#include <limits>
#include <utility>

namespace bf::bfsp {
namespace {
using Type = QJsonValue::Type;

bool field(const QJsonObject& object, const char* name, Type type)
{
    return object.value(QLatin1String(name)).type() == type;
}

bool text(const QJsonObject& object, const char* name)
{
    const auto value = object.value(QLatin1String(name));
    return value.isString() && !value.toString().isEmpty();
}

bool uuid(const QJsonValue& value)
{
    static const QRegularExpression pattern(QStringLiteral(
        "\\A[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z"));
    return value.isString() && pattern.match(value.toString()).hasMatch();
}

bool u32(const QJsonValue& value, bool positive = false)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    return std::isfinite(number) && number >= (positive ? 1.0 : 0.0) &&
           number <= std::numeric_limits<std::uint32_t>::max() && std::floor(number) == number;
}

bool u64_string(const QJsonValue& value, bool positive = false, std::uint64_t* parsed = nullptr)
{
    if (!value.isString()) return false;
    const auto string = value.toString();
    if (string.isEmpty() || string.size() > 20 ||
        (string.size() > 1 && string.front() == QLatin1Char('0'))) return false;
    std::uint64_t result = 0;
    for (const auto c : string) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
        const unsigned digit = c.unicode() - '0';
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        result = result * 10 + digit;
    }
    if (positive && result == 0) return false;
    if (parsed) *parsed = result;
    return true;
}

bool nullable_u64(const QJsonValue& value)
{
    return value.isNull() || u64_string(value);
}

bool optional_u64(const QJsonObject& object, const char* name, bool nullable = false)
{
    const auto key = QLatin1String(name);
    if (!object.contains(key)) return true;
    const auto value = object.value(key);
    return (nullable && value.isNull()) || u64_string(value);
}

bool string_array(const QJsonValue& value)
{
    if (!value.isArray()) return false;
    for (const auto element : value.toArray()) if (!element.isString()) return false;
    return true;
}

bool protocol(const QJsonValue& value)
{
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    return u32(o.value("major")) && o.value("major").toInt() == 1 &&
           u32(o.value("minor")) && o.value("minor").toInt() == 0;
}

bool receive_limits(const QJsonValue& value)
{
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    return u32(o.value("max_payload"), true) && o.value("max_payload").toDouble() <= kMaxPayload &&
           u32(o.value("max_json"), true) && o.value("max_json").toDouble() <= kMaxJson &&
           u32(o.value("max_channels"), true) && o.value("max_channels").toDouble() <= kMaxChannels &&
           u32(o.value("max_streams"), true) && o.value("max_streams").toDouble() <= 64;
}

bool error_object(const QJsonValue& value)
{
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    return text(o, "code") && field(o, "params", Type::Object) && field(o, "retryable", Type::Bool);
}

bool device(const QJsonValue& value)
{
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    for (const char* name : {"device_id", "name", "vendor", "model", "firmware", "identity_strength"}) {
        const auto v = o.value(QLatin1String(name));
        if (!v.isString() && !v.isNull()) return false;
    }
    const auto capabilities = o.value("capabilities");
    if (!capabilities.isObject()) return false;
    const auto c = capabilities.toObject();
    for (const char* name : {"can_configure", "can_start_stop", "supports_hardware_timestamps", "supports_udp"})
        if (!field(c, name, Type::Bool)) return false;
    return true;
}

bool hello(const QJsonObject& o)
{
    return protocol(o.value("protocol")) && text(o, "client_name") && text(o, "client_version") &&
           receive_limits(o.value("receive_limits")) && string_array(o.value("features")) &&
           string_array(o.value("required_features")) && uuid(o.value("nonce"));
}

bool hello_ack(const QJsonObject& o)
{
    if (!field(o, "ok", Type::Bool) || !uuid(o.value("nonce"))) return false;
    if (!o.value("ok").toBool()) return error_object(o.value("error"));
    return protocol(o.value("protocol")) && receive_limits(o.value("receive_limits")) &&
           string_array(o.value("features")) && device(o.value("device")) &&
           uuid(o.value("session_id")) && uuid(o.value("boot_id")) &&
           u32(o.value("heartbeat_ms"), true) && u32(o.value("idle_timeout_ms"), true) &&
           o.value("idle_timeout_ms").toDouble() <= 30'000 &&
           o.value("heartbeat_ms").toDouble() * 3 <= o.value("idle_timeout_ms").toDouble();
}

bool channel_unit(const QString& signal, const QJsonValue& unit)
{
    if (unit.isNull()) return true;
    if (!unit.isString()) return false;
    const auto u = unit.toString();
    static const QSet<QString> units = {"V", "mV", "uV", "A", "mA", "Pa", "kPa", "degC", "1", "count"};
    if (!units.contains(u)) return false;
    if (u == "count") return true;
    if (signal == "eeg" || signal == "eog" || signal == "emg" || signal == "ecg" || signal == "voltage")
        return u == "V" || u == "mV" || u == "uV";
    if (signal == "temperature") return u == "degC";
    if (signal == "pressure") return u == "Pa" || u == "kPa";
    return true;
}

bool descriptor(const QJsonObject& o, std::uint32_t stream_id, Limits limits)
{
    if (!uuid(o.value("session_id")) || !uuid(o.value("capture_id")) ||
        !u32(o.value("stream_id"), true) || o.value("stream_id").toDouble() != stream_id ||
        !u32(o.value("descriptor_revision"), true) || !text(o, "name") || !text(o, "signal_type") ||
        !u32(o.value("rate_num"), true) || !u32(o.value("rate_den"), true) ||
        o.value("layout") != QJsonValue("channel_major") || !u32(o.value("max_samples_per_block"), true))
        return false;
    const auto type = o.value("dtype").toString();
    int width = 0;
    if (type == "int16") width = 2;
    else if (type == "int24") width = 3;
    else if (type == "int32" || type == "float32") width = 4;
    else if (type == "float64") width = 8;
    if (!width || !o.value("channels").isArray()) return false;
    const auto channels = o.value("channels").toArray();
    if (channels.isEmpty() || channels.size() > limits.max_channels) return false;
    const auto max_samples = static_cast<std::uint64_t>(o.value("max_samples_per_block").toDouble());
    const auto max_bytes = std::uint64_t(channels.size()) * max_samples * static_cast<unsigned>(width);
    if (limits.max_payload < kDataPrefixBytes || max_bytes > limits.max_payload - kDataPrefixBytes) return false;
    QSet<QString> ids;
    for (const auto& channel : channels) {
        if (!channel.isObject()) return false;
        const auto c = channel.toObject();
        if (!text(c, "channel_id") || !text(c, "name") || !text(c, "signal_type") ||
            !c.contains("unit") || !channel_unit(c.value("signal_type").toString(), c.value("unit")) ||
            !field(c, "scale", Type::Double) || !field(c, "offset", Type::Double)) return false;
        const double scale = c.value("scale").toDouble();
        const double offset = c.value("offset").toDouble();
        if (!std::isfinite(scale) || !std::isfinite(offset) || scale == 0 ||
            (c.value("unit").isNull() && (scale != 1 || offset != 0))) return false;
        const auto id = c.value("channel_id").toString();
        if (ids.contains(id)) return false;
        ids.insert(id);
    }
    if (!o.value("clock").isObject()) return false;
    const auto clock = o.value("clock").toObject();
    const auto source = clock.value("source").toString();
    const auto epoch = clock.value("epoch").toString();
    if (!u32(clock.value("clock_id")) || !u64_string(clock.value("ticks_per_second_num"), true) ||
        !u64_string(clock.value("ticks_per_second_den"), true) ||
        (source != "hardware" && source != "host_monotonic" && source != "estimated") ||
        (epoch != "boot_relative" && epoch != "session_relative")) return false;
    if (o.contains("sample_index_source")) {
        const auto source_index = o.value("sample_index_source").toString();
        if (source_index != "device" && source_index != "adapter_received") return false;
    }
    return true;
}

bool candidate(const QJsonValue& value)
{
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    if (!field(o, "groups", Type::Array) || !field(o, "device_options", Type::Object)) return false;
    for (const auto& entry : o.value("groups").toArray()) {
        if (!entry.isObject()) return false;
        const auto group = entry.toObject();
        const auto dtype = group.value("dtype").toString();
        if (!text(group, "group_id") || !string_array(group.value("enabled_channel_ids")) ||
            !u32(group.value("rate_num"), true) || !u32(group.value("rate_den"), true) ||
            (dtype != "int16" && dtype != "int24" && dtype != "int32" &&
             dtype != "float32" && dtype != "float64")) return false;
    }
    return true;
}

bool command(const QJsonObject& o)
{
    if (!uuid(o.value("request_id")) || !text(o, "method") || !field(o, "params", Type::Object)) return false;
    const auto method = o.value("method").toString();
    const auto params = o.value("params").toObject();
    if (method == "capture.start" || method == "capture.stop" || method == "capture.discard")
        return uuid(params.value("capture_id"));
    if (method == "config.validate" || method == "config.apply")
        return candidate(params.value("candidate")) && u32(params.value("expected_revision"));
    if (method == "stream.accept") {
        if (!uuid(params.value("capture_id")) || !params.value("streams").isArray()) return false;
        const auto streams = params.value("streams").toArray();
        if (streams.isEmpty() || streams.size() > 64) return false;
        QSet<std::uint32_t> ids;
        for (const auto& entry : streams) {
            if (!entry.isObject()) return false;
            const auto stream = entry.toObject();
            if (!u32(stream.value("stream_id"), true) ||
                !u32(stream.value("descriptor_revision"), true)) return false;
            const auto id = static_cast<std::uint32_t>(stream.value("stream_id").toDouble());
            if (ids.contains(id)) return false;
            ids.insert(id);
        }
        return true;
    }
    if (method == "capture.prepare") return u32(params.value("expected_config_revision"));
    if (method == "time.sync")
        return u32(params.value("clock_id")) && u64_string(params.value("client_send_ns"));
    if (method == "transport.bind_udp")
        return text(params, "address") && u32(params.value("port"), true) &&
               params.value("port").toDouble() <= 65535;
    return true;
}

bool response(const QJsonObject& o)
{
    if (!uuid(o.value("request_id")) || !field(o, "ok", Type::Bool)) return false;
    if (o.value("ok").toBool()) {
        if (!field(o, "result", Type::Object) || o.contains("error")) return false;
        const auto result = o.value("result").toObject();
        if (!optional_u64(result, "client_send_ns") || !optional_u64(result, "receive_ticks") ||
            !optional_u64(result, "send_ticks")) return false;
        if (result.contains("streams")) {
            if (!result.value("streams").isArray()) return false;
            for (const auto& stream : result.value("streams").toArray())
                if (!stream.isObject() || !optional_u64(stream.toObject(), "last_sample_exclusive", true)) return false;
        }
        return true;
    }
    return error_object(o.value("error")) && !o.contains("result");
}

bool event(const QJsonObject& o, std::uint32_t stream_id)
{
    if (!uuid(o.value("event_id")) || !text(o, "kind") || !field(o, "params", Type::Object)) return false;
    if (stream_id && (!u32(o.value("stream_id"), true) || o.value("stream_id").toDouble() != stream_id ||
                      !u32(o.value("descriptor_revision"), true))) return false;
    if (o.contains("stream_id") && (!u32(o.value("stream_id")) || o.value("stream_id").toDouble() != stream_id)) return false;
    if (!optional_u64(o, "sample_index", true) || !optional_u64(o, "count", true) ||
        !optional_u64(o, "ticks", true)) return false;
    const auto params = o.value("params").toObject();
    if (o.value("kind").toString() == "data.gap") {
        if (!stream_id || !o.contains("sample_index") || !o.contains("count") ||
            !text(params, "reason") || !text(params, "detection") ||
            params.contains("start") || params.contains("count")) return false;
        const auto start = o.value("sample_index");
        const auto count = o.value("count");
        std::uint64_t first = 0, length = 0;
        if ((!start.isNull() && !u64_string(start, false, &first)) ||
            (!count.isNull() && !u64_string(count, true, &length))) return false;
        return start.isNull() || count.isNull() ||
               first <= std::numeric_limits<std::uint64_t>::max() - length;
    }
    return optional_u64(params, "start", true) && optional_u64(params, "count", true);
}

bool status(const QJsonObject& o)
{
    if (!text(o, "state") || !o.contains("active_capture_id") ||
        !(o.value("active_capture_id").isNull() || uuid(o.value("active_capture_id"))) ||
        !u32(o.value("config_revision")) || !field(o, "counters", Type::Object)) return false;
    const auto counters = o.value("counters").toObject();
    for (auto i = counters.begin(); i != counters.end(); ++i) if (!u64_string(i.value())) return false;
    return true;
}

bool stream_end(const QJsonObject& o, std::uint32_t stream_id)
{
    return uuid(o.value("capture_id")) && u32(o.value("stream_id"), true) &&
           o.value("stream_id").toDouble() == stream_id && o.contains("last_sample_exclusive") &&
           nullable_u64(o.value("last_sample_exclusive")) && text(o, "reason") && field(o, "complete", Type::Bool);
}
}  // namespace

Result validate_json(MessageType type, std::uint32_t stream_id, QByteArrayView payload,
                     QJsonObject* object, Limits limits)
{
    limits = detail::bounded(limits);
    const auto max_json = (type == MessageType::Hello || type == MessageType::HelloAck)
            ? qMin(limits.max_json, std::uint32_t{4096}) : limits.max_json;
    if (payload.size() > limits.max_payload || payload.size() > max_json)
        return detail::fail(Error::LimitExceeded, "json_length");
    QJsonObject parsed;
    auto result = detail::parse_json(payload, parsed);
    if (!result) return result;
    bool valid = false;
    switch (type) {
    case MessageType::Hello: valid = stream_id == 0 && hello(parsed); break;
    case MessageType::HelloAck: valid = stream_id == 0 && hello_ack(parsed); break;
    case MessageType::Command: valid = stream_id == 0 && command(parsed); break;
    case MessageType::Response: valid = stream_id == 0 && response(parsed); break;
    case MessageType::Descriptor: valid = stream_id && descriptor(parsed, stream_id, limits); break;
    case MessageType::Event: valid = event(parsed, stream_id); break;
    case MessageType::Status: valid = stream_id == 0 && status(parsed); break;
    case MessageType::Heartbeat: valid = stream_id == 0; break;
    case MessageType::StreamEnd: valid = stream_id && stream_end(parsed, stream_id); break;
    case MessageType::Data: break;
    }
    if (!valid) return detail::fail(Error::InvalidPayload, "message_schema");
    if (object) *object = std::move(parsed);
    return {};
}

}  // namespace bf::bfsp
