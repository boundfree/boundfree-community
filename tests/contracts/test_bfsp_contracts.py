"""BFSP's saved wire fixtures and machine-readable candidate contracts."""

import copy
import json
import math
from pathlib import Path
import random
import re
import struct

import pytest


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "contracts" / "bfsp"
VECTORS = json.loads((CONTRACT / "frame-vectors.json").read_text(encoding="utf-8"))
REGISTRY = json.loads((CONTRACT / "registry.json").read_text(encoding="utf-8"))
SCHEMA = json.loads((CONTRACT / "messages.schema.json").read_text(encoding="utf-8"))


def crc32c(data):
    result = 0xFFFFFFFF
    for byte in data:
        result ^= byte
        for _ in range(8):
            result = (result >> 1) ^ (0x82F63B78 if result & 1 else 0)
    return result ^ 0xFFFFFFFF


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate key")
        result[key] = value
    return result


def finite_number(value):
    result = float(value)
    if not math.isfinite(result):
        raise ValueError("non-finite number")
    return result


def reject_constant(value):
    raise ValueError(f"non-finite constant: {value}")


def strict_json(data):
    return json.loads(data, object_pairs_hook=unique_keys, parse_float=finite_number,
                      parse_constant=reject_constant)


def validator(name):
    jsonschema = pytest.importorskip(
        "jsonschema", reason="BFSP schema tests require: python3 -m pip install jsonschema pytest"
    )
    schema = {"$schema": SCHEMA["$schema"], "$defs": SCHEMA["$defs"], "$ref": f"#/$defs/{name}"}
    return jsonschema.Draft202012Validator(schema)


def test_registry_and_vector_wire_contract():
    assert REGISTRY["wire_version"] == {"major": 1, "minor": 0}
    assert (REGISTRY["header_bytes"], REGISTRY["data_prefix_bytes"], REGISTRY["trailer_bytes"]) == (24, 48, 4)
    assert [item["id"] for item in REGISTRY["messages"]] == list(range(1, 11))
    assert {item["name"]: item["bytes"] for item in REGISTRY["dtypes"]} == {
        "int16": 2, "int24": 3, "int32": 4, "float32": 4, "float64": 8,
    }
    assert crc32c(b"123456789") == 0xE3069283
    assert REGISTRY["command_params"]["stream.accept"]["schema_ref"] == "#/$defs/stream_accept_params"
    assert REGISTRY["event_contracts"]["data.gap"]["schema_ref"] == "#/$defs/EVENT"
    for entry in VECTORS["valid_frames"]:
        data = bytes.fromhex(entry["frame_hex"])
        magic, major, kind, flags, header_size, reserved, length, stream, seq = struct.unpack_from("<4sBBHHHIII", data)
        assert (magic, major, flags, header_size, reserved) == (b"BFSP", 1, 0, 24, 0), entry["name"]
        assert len(data) == 28 + length
        assert crc32c(data[:-4]) == struct.unpack_from("<I", data, len(data) - 4)[0]
        expected = entry["expected"]
        assert (kind, stream, seq, length) == (expected["message_type"], expected["stream_id"],
                                               expected["sequence"], expected["payload_length"])
        if kind != 6:
            assert strict_json(data[24:-4]) == expected["json"]


def test_schema_validates_independent_json_vectors():
    by_id = {item["id"]: item for item in REGISTRY["messages"]}
    for entry in VECTORS["valid_frames"]:
        expected = entry["expected"]
        message = by_id[expected["message_type"]]
        if message["encoding"] == "json":
            errors = list(validator(message["name"]).iter_errors(expected["json"]))
            assert not errors, (entry["name"], errors)
    descriptor = json.loads((CONTRACT / "stream-descriptor.json").read_text(encoding="utf-8"))
    assert not list(validator("DESCRIPTOR").iter_errors(descriptor))


def test_schema_rejects_overflow_and_bad_json_types():
    validate = validator("u64")
    for value in ("0", "1", "9007199254740993", "18446744073709551615"):
        assert validate.is_valid(value), value
    for value in ("00", "01", "000000000000000000001", "18446744073709551616", "-1", "+1", "1.0", "1\n", 9007199254740993):
        assert not validate.is_valid(value), value
    validate_i = validator("i64")
    for value in ("-9223372036854775808", "9223372036854775807", "0"):
        assert validate_i.is_valid(value), value
    for value in ("-9223372036854775809", "9223372036854775808", "-0", "01", 1):
        assert not validate_i.is_valid(value), value


def test_integer_patterns_cover_full_range():
    rng = random.Random(4711)
    unsigned_pattern = re.compile(SCHEMA["$defs"]["u64"]["pattern"])
    signed_pattern = re.compile(SCHEMA["$defs"]["i64"]["pattern"])
    for _ in range(2000):
        unsigned = rng.getrandbits(64)
        signed = rng.randrange(-(2**63), 2**63)
        assert unsigned_pattern.search(str(unsigned))
        assert signed_pattern.search(str(signed))
    assert not unsigned_pattern.search(str(2**64))
    assert not signed_pattern.search(str(-(2**63) - 1))


def test_descriptor_and_response_schema_failures():
    descriptor = json.loads((CONTRACT / "stream-descriptor.json").read_text(encoding="utf-8"))
    checks = []
    bad = copy.deepcopy(descriptor)
    bad["clock"]["ticks_per_second_num"] = 1000  # u64 must stay a string
    checks.append(bad)
    bad = copy.deepcopy(descriptor)
    bad["channels"][0].update(unit=None, scale=0.1)
    checks.append(bad)
    bad = copy.deepcopy(descriptor)
    bad["channels"][0]["unit"] = "degC"
    checks.append(bad)
    bad = copy.deepcopy(descriptor)
    bad["rate_den"] = 0
    checks.append(bad)
    for item in checks:
        assert not validator("DESCRIPTOR").is_valid(item)
    response = {"request_id": "44444444-4444-4444-8444-444444444444", "ok": True,
                "result": {}, "error": {"code": "device.busy", "params": {}, "retryable": False}}
    assert not validator("RESPONSE").is_valid(response)
    response.pop("error")
    response["result"] = {"receive_ticks": 123}
    assert not validator("RESPONSE").is_valid(response)
    response["result"] = {"receive_ticks": "9007199254740993"}
    assert validator("RESPONSE").is_valid(response)


def test_json_duplicate_and_nonfinite_are_rejected_before_schema():
    for payload in (b'{"x":1,"x":2}', b'{"x":1,"\\u0078":2}', b'{"x":1e999}', b'{"x":NaN}'):
        with pytest.raises(ValueError):
            strict_json(payload)


def test_event_id_must_be_uuid():
    event = {"event_id": "11111111-1111-4111-8111-111111111111", "kind": "trigger.edge", "params": {}}
    assert validator("EVENT").is_valid(event)
    for bad in ("", "not-a-uuid", "11111111-1111-4111-8111-11111111111A", 1):
        assert not validator("EVENT").is_valid(dict(event, event_id=bad))
    gap = dict(event, kind="data.gap", stream_id=1, descriptor_revision=1, sample_index="3", count="2",
               params={"reason": "loss", "detection": "counter"})
    assert validator("EVENT").is_valid(gap)
    assert validator("EVENT").is_valid(dict(gap, sample_index=None, count=None))
    assert validator("EVENT").is_valid(dict(gap, sample_index=None, count="2"))
    assert not validator("EVENT").is_valid(dict(gap, count="0"))
    assert not validator("EVENT").is_valid(dict(gap, count=2))
    assert not validator("EVENT").is_valid(dict(gap, stream_id=0))
    assert not validator("EVENT").is_valid(dict(gap, params={"reason": "loss"}))
    assert not validator("EVENT").is_valid(dict(gap, params={"reason": "loss", "detection": "counter", "start": "3"}))
    missing = dict(gap)
    del missing["sample_index"]
    assert not validator("EVENT").is_valid(missing)


def test_config_candidate_schema():
    candidate = {"groups": [{"group_id": "analog-0", "enabled_channel_ids": ["ch1"],
                             "rate_num": 250, "rate_den": 1, "dtype": "int24"}], "device_options": {}}
    request = {"request_id": "44444444-4444-4444-8444-444444444444", "method": "config.validate",
               "params": {"candidate": candidate, "expected_revision": 1}}
    assert validator("COMMAND").is_valid(request)
    candidate["groups"][0]["rate_den"] = 0
    assert not validator("COMMAND").is_valid(request)
    capture_id = "22222222-2222-4222-8222-222222222222"
    stream = {"stream_id": 1, "descriptor_revision": 1}
    request.update(method="stream.accept", params={"capture_id": capture_id, "streams": [stream]})
    assert validator("COMMAND").is_valid(request)
    request["params"]["streams"] = []
    assert not validator("COMMAND").is_valid(request)
    request["params"]["streams"] = [stream, dict(stream)]
    assert not validator("COMMAND").is_valid(request)
    request["params"]["streams"] = [{"stream_id": 1, "descriptor_revision": 0}]
    assert not validator("COMMAND").is_valid(request)
    request.update(method="transport.bind_udp", params={"address": "127.0.0.1", "port": 65535})
    assert validator("COMMAND").is_valid(request)
    for params in ({"address": "", "port": 45120}, {"address": "127.0.0.1", "port": 0},
                   {"address": "127.0.0.1", "port": 65536}, {"address": "127.0.0.1", "port": 1.5}):
        request["params"] = params
        assert not validator("COMMAND").is_valid(request)
    for method in ("device.get_info", "device.get_status", "device.disconnect"):
        request.update(method=method, params={})
        assert validator("COMMAND").is_valid(request)
        request["params"] = {"vendor.extension": True}
        assert validator("COMMAND").is_valid(request)
