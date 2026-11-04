#include "codec.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

using namespace bf::bfsp;

namespace {
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

QByteArray fixture(const QJsonObject& value) { return QByteArray::fromHex(value.value("frame_hex").toString().toLatin1()); }

void set_le(QByteArray& bytes, qsizetype offset, std::uint64_t number, int width)
{
    for (int i = 0; i < width; ++i) bytes[offset + i] = static_cast<char>((number >> (i * 8)) & 255);
}

QByteArray unchecked_heartbeat(QByteArrayView payload)
{
    QByteArray bytes;
    check(bool(encode_frame(MessageType::Heartbeat, 0, 0, QByteArrayView("{}", 2), bytes)), "encode heartbeat");
    bytes.resize(kHeaderBytes);
    set_le(bytes, 12, payload.size(), 4);
    bytes.append(payload.data(), payload.size());
    const auto crc = crc32c(QByteArrayView(bytes));
    bytes.resize(bytes.size() + 4);
    set_le(bytes, bytes.size() - 4, crc, 4);
    return bytes;
}

QByteArray json_bytes(const QJsonObject& value)
{
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}

void vectors_test(const QJsonObject& root, QByteArray& data_frame, QByteArray& heartbeat,
                  QJsonObject& descriptor)
{
    check(crc32c(QByteArrayView("123456789", 9)) == 0xe3069283u, "CRC-32C standard vector");
    int valid = 0;
    for (const auto& entry : root.value("valid_frames").toArray()) {
        const auto item = entry.toObject();
        const auto input = fixture(item);
        Frame frame;
        const auto status = decode_frame(QByteArrayView(input), frame);
        if (!status) throw std::runtime_error((item.value("name").toString() + ": " + status.detail).toStdString());
        const auto expected = item.value("expected").toObject();
        check(static_cast<int>(frame.header.type) == expected.value("message_type").toInt(), "message type");
        check(frame.header.stream_id == static_cast<unsigned>(expected.value("stream_id").toInt()), "stream id");
        check(frame.header.sequence == static_cast<unsigned>(expected.value("sequence").toInt()), "sequence");
        check(frame.payload.size() == expected.value("payload_length").toInt(), "payload length");
        QByteArray encoded;
        check(bool(encode_frame(frame.header.type, frame.header.stream_id, frame.header.sequence,
                                QByteArrayView(frame.payload), encoded)), "encode valid frame");
        check(encoded == input, "C++ output differs from independent vector");
        if (frame.header.type == MessageType::Data) {
            DataPrefix prefix;
            check(bool(decode_data(QByteArrayView(frame.payload), prefix)), "decode prefix");
            const auto rows = expected.value("data").toObject().value("channel_values").toArray();
            check(prefix.channel_count == static_cast<unsigned>(rows.size()), "channels");
            for (qsizetype channel = 0; channel < rows.size(); ++channel) {
                const auto values = rows[channel].toArray();
                check(prefix.sample_count == static_cast<unsigned>(values.size()), "samples");
                for (qsizetype sample = 0; sample < values.size(); ++sample) {
                    double decoded = 0;
                    check(bool(sample_as_double(QByteArrayView(frame.payload), prefix,
                                                channel * prefix.sample_count + sample, decoded)), "sample decode");
                    check(decoded == values[sample].toDouble(), "sample value");
                }
            }
            if (prefix.dtype == DType::Int24) data_frame = input;
        }
        if (item.value("name") == QJsonValue("heartbeat_empty")) heartbeat = input;
        if (item.value("name") == QJsonValue("descriptor_int24"))
            descriptor = expected.value("json").toObject();
        ++valid;
    }
    int invalid = 0;
    for (const auto& entry : root.value("invalid_frames").toArray()) {
        const auto item = entry.toObject();
        const auto bytes = fixture(item);
        Frame frame;
        if (decode_frame(QByteArrayView(bytes), frame))
            throw std::runtime_error(("accepted invalid vector: " + item.value("name").toString()).toStdString());
        ++invalid;
    }
    check(valid == 11 && invalid == 10 && !data_frame.isEmpty() && !heartbeat.isEmpty(), "fixture count");
}

void payload_test(const QJsonObject& reference, const QByteArray& data_frame)
{
    auto descriptor = reference;
    auto clock = descriptor.value("clock").toObject();
    clock["ticks_per_second_num"] = "18446744073709551615";
    descriptor["clock"] = clock;
    check(bool(validate_json(MessageType::Descriptor, 1, QByteArrayView(json_bytes(descriptor)))), "max u64 string");
    clock["ticks_per_second_num"] = "9007199254740993";
    descriptor["clock"] = clock;
    check(bool(validate_json(MessageType::Descriptor, 1, QByteArrayView(json_bytes(descriptor)))), "u64 above 2^53");
    clock["ticks_per_second_num"] = "01";
    descriptor["clock"] = clock;
    check(!validate_json(MessageType::Descriptor, 1, QByteArrayView(json_bytes(descriptor))), "u64 leading zero");
    clock["ticks_per_second_num"] = "000000000000000000001";
    descriptor["clock"] = clock;
    check(!validate_json(MessageType::Descriptor, 1, QByteArrayView(json_bytes(descriptor))), "u64 length limit");
    clock["ticks_per_second_num"] = "18446744073709551616";
    descriptor["clock"] = clock;
    check(!validate_json(MessageType::Descriptor, 1, QByteArrayView(json_bytes(descriptor))), "overflow u64");
    clock["ticks_per_second_num"] = 9007199254740992.0;
    descriptor["clock"] = clock;
    check(!validate_json(MessageType::Descriptor, 1, QByteArrayView(json_bytes(descriptor))), "u64 numeric type");
    QJsonObject event{{"event_id", "11111111-1111-4111-8111-111111111111"},
                      {"kind", "trigger.edge"}, {"params", QJsonObject{}}, {"sample_index", "0"}};
    check(bool(validate_json(MessageType::Event, 0, QByteArrayView(json_bytes(event)))), "event UUID and zero u64");
    event["event_id"] = "not-a-uuid";
    check(!validate_json(MessageType::Event, 0, QByteArrayView(json_bytes(event))), "invalid event UUID");
    QJsonObject response{{"request_id", "44444444-4444-4444-8444-444444444444"}, {"ok", true},
                         {"result", QJsonObject{{"receive_ticks", "9007199254740993"}}}};
    check(bool(validate_json(MessageType::Response, 0, QByteArrayView(json_bytes(response)))), "response u64");
    response["result"] = QJsonObject{{"receive_ticks", 123}};
    check(!validate_json(MessageType::Response, 0, QByteArrayView(json_bytes(response))), "response numeric u64");
    const QJsonObject group{{"group_id", "analog-0"}, {"enabled_channel_ids", QJsonArray{"ch1"}},
                            {"rate_num", 250}, {"rate_den", 1}, {"dtype", "int24"}};
    const QJsonObject candidate{{"groups", QJsonArray{group}}, {"device_options", QJsonObject{}}};
    QJsonObject request{{"request_id", "44444444-4444-4444-8444-444444444444"}, {"method", "config.validate"},
                        {"params", QJsonObject{{"candidate", candidate}, {"expected_revision", 1}}}};
    check(bool(validate_json(MessageType::Command, 0, QByteArrayView(json_bytes(request)))), "config candidate");
    request["params"] = QJsonObject{{"candidate", QJsonObject{}}, {"expected_revision", 1}};
    check(!validate_json(MessageType::Command, 0, QByteArrayView(json_bytes(request))), "config candidate missing groups");

    const QString capture = "22222222-2222-4222-8222-222222222222";
    const auto command_valid = [&](const QString& method, const QJsonObject& params) {
        const QJsonObject command{{"request_id", "44444444-4444-4444-8444-444444444444"},
                                  {"method", method}, {"params", params}};
        return bool(validate_json(MessageType::Command, 0, QByteArrayView(json_bytes(command))));
    };
    const QJsonObject stream1{{"stream_id", 1}, {"descriptor_revision", 1}};
    const QJsonObject stream2{{"stream_id", 2}, {"descriptor_revision", 3}};
    check(command_valid("stream.accept", {{"capture_id", capture}, {"streams", QJsonArray{stream1, stream2}}}),
          "stream accept valid list");
    check(!command_valid("stream.accept", {{"capture_id", capture}}), "stream accept missing list");
    check(!command_valid("stream.accept", {{"capture_id", capture}, {"streams", QJsonArray{}}}),
          "stream accept empty list");
    check(!command_valid("stream.accept", {{"capture_id", capture}, {"streams", QJsonArray{stream1, stream1}}}),
          "stream accept duplicate entry");
    const QJsonObject same_id{{"stream_id", 1}, {"descriptor_revision", 2}};
    check(!command_valid("stream.accept", {{"capture_id", capture}, {"streams", QJsonArray{stream1, same_id}}}),
          "stream accept duplicate id with different revision");
    check(!command_valid("stream.accept", {{"capture_id", capture},
                        {"streams", QJsonArray{QJsonObject{{"stream_id", 1}, {"descriptor_revision", 0}}}}}),
          "stream accept zero revision");
    QJsonArray many_streams;
    for (int i = 1; i <= 65; ++i) many_streams.append(QJsonObject{{"stream_id", i}, {"descriptor_revision", 1}});
    check(!command_valid("stream.accept", {{"capture_id", capture}, {"streams", many_streams}}),
          "stream accept max streams");
    check(command_valid("transport.bind_udp", {{"address", "127.0.0.1"}, {"port", 45120}}), "UDP endpoint");
    check(!command_valid("transport.bind_udp", {{"address", ""}, {"port", 45120}}), "UDP address");
    check(!command_valid("transport.bind_udp", {{"address", "127.0.0.1"}, {"port", 0}}), "UDP zero port");
    check(!command_valid("transport.bind_udp", {{"address", "127.0.0.1"}, {"port", 65536}}), "UDP port limit");
    for (const auto& method : {"device.get_info", "device.get_status", "device.disconnect"}) {
        check(command_valid(method, {}), "parameter-free command");
        check(command_valid(method, {{"vendor.extension", true}}), "parameter-free command extension");
    }

    QJsonObject gap{{"event_id", "11111111-1111-4111-8111-111111111111"}, {"kind", "data.gap"},
                    {"stream_id", 1}, {"descriptor_revision", 2}, {"sample_index", "18446744073709551614"},
                    {"count", "1"}, {"params", QJsonObject{{"reason", "loss"}, {"detection", "counter"}}}};
    const auto gap_valid = [&](const QJsonObject& item, std::uint32_t stream_id = 1) {
        return bool(validate_json(MessageType::Event, stream_id, QByteArrayView(json_bytes(item))));
    };
    check(gap_valid(gap), "known gap ending at u64 max");
    auto changed_gap = gap;
    changed_gap["sample_index"] = QJsonValue::Null;
    changed_gap["count"] = QJsonValue::Null;
    check(gap_valid(changed_gap), "unknown gap bounds");
    changed_gap = gap;
    changed_gap["count"] = "0";
    check(!gap_valid(changed_gap), "gap zero count");
    changed_gap = gap;
    changed_gap["sample_index"] = "18446744073709551615";
    check(!gap_valid(changed_gap), "gap interval overflow");
    changed_gap = gap;
    changed_gap.remove("sample_index");
    check(!gap_valid(changed_gap), "gap missing top-level start");
    changed_gap = gap;
    changed_gap["params"] = QJsonObject{{"reason", "loss"}};
    check(!gap_valid(changed_gap), "gap missing detection");
    changed_gap = gap;
    changed_gap["params"] = QJsonObject{{"reason", "loss"}, {"detection", "counter"}, {"start", "1"}};
    check(!gap_valid(changed_gap), "gap legacy nested start");
    check(!gap_valid(gap, 0) && !gap_valid(gap, 2), "gap stream mismatch");
    const QByteArray duplicate = R"({"x":1,"\u0078":2})";
    check(!validate_json(MessageType::Heartbeat, 0, QByteArrayView(duplicate)), "escaped duplicate key");
    QByteArray deep = "{";
    for (int i = 0; i < 16; ++i) deep += "\"x\":{";
    deep += "\"a\":1";
    for (int i = 0; i < 17; ++i) deep += '}';
    check(!validate_json(MessageType::Heartbeat, 0, QByteArrayView(deep)), "depth limit");
    check(!validate_json(MessageType::Heartbeat, 0, QByteArrayView("{\"x\":1e999}", 11)), "nonfinite JSON");
    check(!validate_json(MessageType::Heartbeat, 0, QByteArrayView("{\"x\":\"\xff\"}", 9)), "invalid UTF-8");
    const QByteArray lone_surrogate = R"({"x":"\ud800"})";
    check(!validate_json(MessageType::Heartbeat, 0, QByteArrayView(lone_surrogate)), "unpaired surrogate");
    QByteArray too_many = "{\"x\":[";
    for (int i = 0; i < 4097; ++i) { if (i) too_many += ','; too_many += '0'; }
    too_many += "]}";
    check(!validate_json(MessageType::Heartbeat, 0, QByteArrayView(too_many)), "array limit");
    QByteArray too_long = "{\"x\":\"" + QByteArray(4097, 'a') + "\"}";
    check(!validate_json(MessageType::Heartbeat, 0, QByteArrayView(too_long)), "string limit");
    Frame decoded;
    check(bool(decode_frame(QByteArrayView(data_frame), decoded)), "int24 fixture");
    auto payload = decoded.payload;
    set_le(payload, 16, std::numeric_limits<std::uint64_t>::max(), 8);
    DataPrefix prefix;
    check(!decode_data(QByteArrayView(payload), prefix), "sample index overflow");
    payload = decoded.payload;
    payload[6] = 2;
    check(!decode_data(QByteArrayView(payload), prefix), "estimated without timestamp");
    payload = decoded.payload;
    payload[40] = 1;
    check(!decode_data(QByteArrayView(payload), prefix), "reserved data bit");

    const auto single_sample = [](DType type, int width, std::uint64_t bits) {
        QByteArray bytes(kDataPrefixBytes + width, '\0');
        set_le(bytes, 0, 1, 4);
        bytes[4] = static_cast<char>(type);
        set_le(bytes, 8, 1, 4);
        set_le(bytes, 12, 1, 4);
        set_le(bytes, kDataPrefixBytes, bits, width);
        return bytes;
    };
    for (const auto& test : {std::pair<DType, std::uint64_t>{DType::Int16, 0x8000},
                             {DType::Int32, 0x80000000}}) {
        const int width = test.first == DType::Int16 ? 2 : 4;
        const auto bytes = single_sample(test.first, width, test.second);
        check(bool(decode_data(QByteArrayView(bytes), prefix)), "integer prefix");
        double value = 0;
        check(bool(sample_as_double(QByteArrayView(bytes), prefix, 0, value)), "integer sample");
        check(value == (width == 2 ? -32768.0 : -2147483648.0), "integer sign extension");
    }
    for (const auto& test : {std::pair<DType, std::uint64_t>{DType::Float32, 0x7f800000},
                             {DType::Float64, UINT64_C(0x7ff8000000000000)}}) {
        const auto bytes = single_sample(test.first, test.first == DType::Float32 ? 4 : 8, test.second);
        check(!decode_data(QByteArrayView(bytes), prefix), "nonfinite floating sample");
    }
    const auto float64 = single_sample(DType::Float64, 8, UINT64_C(0xbff8000000000000));
    check(bool(decode_data(QByteArrayView(float64), prefix)), "finite float64 prefix");
    double value = 0;
    check(bool(sample_as_double(QByteArrayView(float64), prefix, 0, value)) && value == -1.5,
          "float64 decode");
}

void parser_test(const QByteArray& data, const QByteArray& heartbeat)
{
    for (qsizetype split = 0; split <= data.size(); ++split) {
        StreamParser parser;
        auto a = parser.feed(QByteArrayView(data).first(split), 0);
        auto b = parser.feed(QByteArrayView(data).sliced(split), 1);
        check(a.consumed == split && b.consumed == data.size() - split, "split consumed");
        check(a.frames.size() + b.frames.size() == 1 && !b.faulted, "split frame");
    }
    StreamParser parser;
    const auto joined = QByteArray("noise") + heartbeat + data + heartbeat;
    const auto result = parser.feed(QByteArrayView(joined), 0);
    check(result.frames.size() == 3 && result.consumed == joined.size(), "noise and coalesced frames");
    check(parser.diagnostics().skipped_bytes == 5, "noise count");

    QByteArray corrupted = data;
    corrupted[corrupted.size() - 1] ^= 1;
    parser.reset();
    const auto recovery = parser.feed(QByteArrayView(corrupted + heartbeat), 0);
    check(recovery.frames.size() == 1 && parser.diagnostics().invalid_crc == 1, "CRC recovery");

    auto different_major = heartbeat;
    different_major[4] = 2;
    different_major[5] = static_cast<char>(255);
    different_major[6] = 1;  // A new major may assign new types and flags.
    set_le(different_major, different_major.size() - 4,
           crc32c(QByteArrayView(different_major).first(different_major.size() - 4)), 4);
    parser.reset();
    check(!parser.feed(QByteArrayView(different_major).first(kHeaderBytes), 0).faulted,
          "partial different major is not confirmed");
    const auto version = parser.feed(QByteArrayView(different_major).sliced(kHeaderBytes), 1);
    check(version.faulted && !version.errors.empty() && version.errors.front().error == Error::VersionMismatch,
          "valid different major must disconnect");
    different_major[different_major.size() - 1] ^= 1;
    parser.reset();
    const auto false_version = parser.feed(QByteArrayView(different_major + heartbeat), 0);
    check(!false_version.faulted && false_version.frames.size() == 1 && parser.diagnostics().invalid_crc == 1,
          "damaged magic-like candidate must resynchronize");

    ParserOptions options;
    options.max_frames_per_feed = 3;
    options.frame_timeout_ms = 10;
    StreamParser bounded(options);
    const auto bad = unchecked_heartbeat(QByteArrayView(R"({"a":"BFSP","a":1})"));
    QByteArray many;
    for (int i = 0; i < 25; ++i) many += bad;
    many += heartbeat;
    const auto first = bounded.feed(QByteArrayView(many), 0);
    check(first.consumed == many.size() && first.frames.empty() && first.errors.size() == 3, "bad frame budget");
    check(bounded.diagnostics().invalid_payloads == 3 && bounded.buffered_bytes() > 0, "bad frames buffered");
    std::size_t errors = first.errors.size();
    std::size_t frames = 0;
    while (bounded.buffered_bytes()) {
        const auto drained = bounded.feed({}, 100);
        check(!drained.faulted && drained.errors.size() + drained.frames.size() <= 3, "drain budget");
        errors += drained.errors.size();
        frames += drained.frames.size();
    }
    check(errors == 25 && frames == 1 && bounded.diagnostics().invalid_headers == 0 &&
          bounded.diagnostics().timeouts == 0, "invalid payload discard and backlog timing");

    StreamParser timeouts(options);
    check(timeouts.feed(QByteArrayView("B", 1), 0).consumed == 1, "first magic byte");
    check(timeouts.feed(QByteArrayView("F", 1), 4).consumed == 1, "second magic byte");
    check(timeouts.feed(QByteArrayView("S", 1), 8).consumed == 1, "third magic byte");
    const auto timed = timeouts.feed(QByteArrayView("P", 1), 10);
    check(!timed.errors.empty() && timed.errors.front().error == Error::Timeout && timed.frames.empty(), "absolute timeout");

    StreamParser timeout_fault(options);
    for (qint64 t = 0; t < 30; t += 10) {
        timeout_fault.feed(QByteArrayView("B", 1), t);
        const auto poll = timeout_fault.feed({}, t + 10);
        check(poll.faulted == (t == 20), "three consecutive timeouts");
    }

    StreamParser fault;
    QByteArray invalid_crc;
    for (int i = 0; i < 8; ++i) invalid_crc += corrupted;
    auto rejected = fault.feed(QByteArrayView(invalid_crc), 0);
    check(rejected.faulted && fault.diagnostics().consecutive_bad == 8, "bad candidate threshold");
    check(fault.feed(QByteArrayView(heartbeat), 1).faulted, "fault requires reset");
    fault.reset();
    check(fault.feed(QByteArrayView(heartbeat), 1).frames.size() == 1, "reset parser");

    std::mt19937 random(4711);
    for (int iteration = 0; iteration < 100; ++iteration) {
        QByteArray input;
        for (int frame = 0; frame < 5; ++frame) {
            for (unsigned i = 0, n = random() % 16; i < n; ++i) {
                char byte = static_cast<char>(random() & 0xff);
                input += byte == 'B' ? 'x' : byte;
            }
            input += frame % 2 ? data : heartbeat;
        }
        StreamParser random_parser;
        qsizetype position = 0;
        int received = 0;
        while (position < input.size()) {
            const auto length = std::min<qsizetype>(1 + random() % 31, input.size() - position);
            const auto batch = random_parser.feed(QByteArrayView(input).sliced(position, length), position);
            check(batch.consumed == length && !batch.faulted && batch.errors.empty(), "random split");
            received += static_cast<int>(batch.frames.size());
            position += batch.consumed;
        }
        check(received == 5 && random_parser.buffered_bytes() == 0, "random split frame count");
    }
}
}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        QString path = argc > 1 ? QString::fromLocal8Bit(argv[1]) : qEnvironmentVariable("BF_REPO_ROOT");
#ifdef BF_REPO_ROOT
        if (path.isEmpty()) path = QString::fromUtf8(BF_REPO_ROOT);
#endif
        if (path.isEmpty()) throw std::runtime_error("provide repository root or frame-vectors.json, or set BF_REPO_ROOT");
        if (QFileInfo(path).isDir()) path = QDir(path).filePath("contracts/bfsp/frame-vectors.json");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(("cannot read vectors: " + path).toStdString());
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
        check(error.error == QJsonParseError::NoError && doc.isObject(), "vector JSON");
        QByteArray data, heartbeat;
        QJsonObject descriptor;
        vectors_test(doc.object(), data, heartbeat, descriptor);
        payload_test(descriptor, data);
        parser_test(data, heartbeat);
        std::cout << "PASS: BFSP vectors, payload validation, incremental parser and budgets\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
