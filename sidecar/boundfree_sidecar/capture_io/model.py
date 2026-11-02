"""Validation and decoding for raw capture.v1 messages."""

import json
import math
import re
from uuid import UUID

MAX_U64 = (1 << 64) - 1
MAX_BLOCK_BYTES = 1024 * 1024
MAX_JSON_BYTES = 65536
DTYPES = {"int16": (2, "<i2"), "int24": (3, "<i4"), "int32": (4, "<i4"),
          "float32": (4, "<f4"), "float64": (8, "<f8")}
IDENTITY_FIELDS = ("device_id", "boot_id", "session_id", "capture_id", "generation", "stream_id", "descriptor_revision")
CREDENTIAL_FIELDS = {"password", "wifi_password", "token", "access_token", "refresh_token", "api_key",
                     "secret", "client_secret", "private_key", "authorization", "credential", "credentials"}


class CaptureError(Exception):
    def __init__(self, code, params=None):
        self.code = code
        self.params = params or {}
        super().__init__(code)


def invalid(field, reason):
    raise CaptureError("protocol.invalid_params", {"field": field, "reason": reason})


def obj(value, field):
    if not isinstance(value, dict):
        invalid(field, "required object")
    return value


def string(value, field):
    try:
        valid = isinstance(value, str) and bool(value) and len(value.encode("utf-8")) <= 4096
    except UnicodeError:
        valid = False
    if not valid:
        invalid(field, "required non-empty string, at most 4096 bytes")
    return value


def uuid(value, field):
    value = string(value, field)
    try:
        if str(UUID(value)) != value:
            raise ValueError
    except (ValueError, AttributeError):
        invalid(field, "required lowercase UUID")
    return value


def u32(value, field, minimum=0, maximum=(1 << 32) - 1):
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        invalid(field, "integer outside allowed range")
    return value


def u64(value, field, allow_none=False):
    if value is None and allow_none:
        return None
    if not isinstance(value, str) or not re.fullmatch(r"(?:0|[1-9][0-9]*)", value) or len(value) > 20:
        invalid(field, "required unsigned decimal string")
    number = int(value)
    if number > MAX_U64:
        invalid(field, "outside u64 range")
    return number


def i64(value, field):
    if not isinstance(value, str) or not re.fullmatch(r"(?:0|[1-9][0-9]*|-[1-9][0-9]*)", value) or len(value) > 20:
        invalid(field, "required signed decimal string")
    number = int(value)
    if not -(1 << 63) <= number < (1 << 63):
        invalid(field, "outside i64 range")
    return number


def json_value(value, field):
    try:
        pending = [(value, 0)]
        while pending:
            current, depth = pending.pop()
            if depth > 16:
                invalid(field, "JSON exceeds depth limit")
            if isinstance(current, str) and len(current.encode("utf-8")) > 4096:
                invalid(field, "JSON string exceeds size limit")
            if isinstance(current, (list, tuple)):
                if len(current) > 4096:
                    invalid(field, "JSON array exceeds size limit")
                pending.extend((item, depth + 1) for item in current)
            elif isinstance(current, dict):
                if len(current) > 4096 or any(not isinstance(key, str) or len(key.encode("utf-8")) > 4096 for key in current):
                    invalid(field, "invalid JSON object keys")
                pending.extend((item, depth + 1) for item in current.values())
        encoded = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    except (ValueError, TypeError, UnicodeError, RecursionError, OverflowError):
        invalid(field, "must be finite JSON")
    if len(encoded) > MAX_JSON_BYTES:
        invalid(field, "JSON exceeds size limit")
    return encoded


def validate_acquisition_metadata(value):
    field = "acquisition_metadata"
    value = obj(value, field)
    if len(value) > 64:
        invalid(field, "at most 64 fields")
    total = 0
    for key, item in value.items():
        if not isinstance(key, str) or not isinstance(item, str):
            invalid(field, "keys and values must be strings")
        try:
            key_bytes = len(key.encode("utf-8"))
            value_bytes = len(item.encode("utf-8"))
        except UnicodeError:
            invalid(field, "keys and values must be UTF-8")
        if key_bytes > 128 or value_bytes > 2048:
            invalid(field, "key or value exceeds size limit")
        if key.casefold() in CREDENTIAL_FIELDS:
            invalid(field, "credential fields are not permitted")
        total += key_bytes + value_bytes
    if total > 16 * 1024:
        invalid(field, "UTF-8 content exceeds size limit")
    return value


def identity(value, field="stream"):
    value = obj(value, field)
    out = {"device_id": string(value.get("device_id"), f"{field}.device_id")}
    for name in ("boot_id", "session_id", "capture_id"):
        out[name] = uuid(value.get(name), f"{field}.{name}")
    u64(value.get("generation"), f"{field}.generation")
    out["generation"] = value["generation"]
    out["stream_id"] = u32(value.get("stream_id"), f"{field}.stream_id", 1)
    out["descriptor_revision"] = u32(value.get("descriptor_revision"), f"{field}.descriptor_revision", 1)
    return out


def descriptor(value):
    value = obj(value, "descriptor")
    identity(value, "descriptor")
    u64(value.get("record_start_sample_index", "0"), "descriptor.record_start_sample_index")
    string(value.get("name"), "descriptor.name")
    string(value.get("signal_type"), "descriptor.signal_type")
    u32(value.get("rate_num"), "descriptor.rate_num", 1)
    u32(value.get("rate_den"), "descriptor.rate_den", 1)
    u32(value.get("max_samples_per_block"), "descriptor.max_samples_per_block", 1)
    if not isinstance(value.get("dtype"), str) or value["dtype"] not in DTYPES:
        invalid("descriptor.dtype", "unsupported encoding")
    if value.get("layout") != "channel_major":
        invalid("descriptor.layout", "required channel_major")
    channels = value.get("channels")
    if not isinstance(channels, list) or not 1 <= len(channels) <= 4096:
        invalid("descriptor.channels", "expected 1..4096 channels")
    ids = set()
    for channel in channels:
        obj(channel, "channel")
        name = string(channel.get("channel_id"), "channel.channel_id")
        if name in ids:
            invalid("channel.channel_id", "duplicate channel")
        ids.add(name)
        string(channel.get("name"), "channel.name")
        string(channel.get("signal_type"), "channel.signal_type")
        unit = channel.get("unit")
        if unit not in (None, "V", "mV", "uV", "A", "mA", "Pa", "kPa", "degC", "1", "count"):
            invalid("channel.unit", "unsupported unit")
        for key in ("scale", "offset"):
            v = channel.get(key)
            if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v):
                invalid(f"channel.{key}", "required finite number")
        if channel["scale"] == 0 or (unit is None and (channel["scale"] != 1 or channel["offset"] != 0)):
            invalid("channel.scale", "invalid calibration")
    clock = obj(value.get("clock"), "descriptor.clock")
    u32(clock.get("clock_id"), "clock.clock_id")
    if not u64(clock.get("ticks_per_second_num"), "clock.ticks_per_second_num") or not u64(clock.get("ticks_per_second_den"), "clock.ticks_per_second_den"):
        invalid("clock.ticks_per_second", "must be positive")
    if clock.get("source") not in ("hardware", "host_monotonic", "estimated"):
        invalid("clock.source", "unsupported source")
    if clock.get("epoch") not in ("boot_relative", "session_relative"):
        invalid("clock.epoch", "unsupported epoch")
    json_value(value, "descriptor")
    return value


def open_params(params):
    # Own a snapshot: callers must not be able to mutate accepted descriptors.
    params = json.loads(json_value(params, "capture_open"))
    metadata = validate_acquisition_metadata(params["acquisition_metadata"]) if "acquisition_metadata" in params else None
    directory = string(params.get("out_dir"), "out_dir")
    host = obj(params.get("host_clock"), "host_clock")
    string(host.get("source"), "host_clock.source")
    string(host.get("boot_id"), "host_clock.boot_id")
    u64(host.get("monotonic_base_ns"), "host_clock.monotonic_base_ns")
    streams = params.get("streams")
    if not isinstance(streams, list) or not 1 <= len(streams) <= 64:
        invalid("streams", "expected 1..64 descriptors")
    keys, clocks = set(), {}
    for stream in streams:
        descriptor(stream)
        stream.setdefault("record_start_sample_index", "0")
        key = (stream["session_id"], stream["stream_id"])
        if key in keys:
            invalid("streams", "duplicate session_id and stream_id")
        keys.add(key)
        clock_key = (stream["session_id"], stream["clock"]["clock_id"])
        if clock_key in clocks and clocks[clock_key] != stream["clock"]:
            invalid("streams", "inconsistent clock definition")
        clocks[clock_key] = stream["clock"]
    return directory, host, streams, metadata


def block_params(params, desc, payload):
    if identity(params.get("stream")) != identity(desc, "descriptor"):
        invalid("stream", "identity or revision does not match")
    shape = params.get("shape")
    if not isinstance(shape, list) or len(shape) != 2:
        invalid("shape", "required [channels, samples]")
    channels = u32(shape[0], "shape[0]", 1, 4096)
    count = u32(shape[1], "shape[1]", 1)
    if channels != len(desc["channels"]) or count != params.get("sample_count") or count > desc["max_samples_per_block"]:
        invalid("shape", "shape or sample_count does not match descriptor")
    u32(params.get("sample_count"), "sample_count", 1)
    dtype = params.get("dtype")
    if dtype != desc["dtype"]:
        invalid("dtype", "does not match descriptor")
    expected = channels * count * DTYPES[dtype][0]
    if expected > MAX_BLOCK_BYTES or payload is None or len(payload) != expected:
        invalid("payload", "length mismatch or block exceeds limit")
    first = u64(params.get("first_sample_index"), "first_sample_index")
    if first + count > MAX_U64:
        invalid("first_sample_index", "exclusive end outside u64 range")
    timestamp = u64(params.get("first_timestamp_ticks"), "first_timestamp_ticks", True)
    host = u64(params.get("host_received_monotonic_ns"), "host_received_monotonic_ns")
    clock_id = u32(params.get("clock_id"), "clock_id")
    if clock_id != desc["clock"]["clock_id"]:
        invalid("clock_id", "does not match descriptor")
    time_flags = u32(params.get("time_flags"), "time_flags", 0, 3)
    quality_flags = u32(params.get("quality_flags"), "quality_flags", 0, 7)
    if bool(time_flags & 1) != (timestamp is not None) or (time_flags & 2 and not time_flags & 1):
        invalid("time_flags", "inconsistent timestamp")
    if time_flags & 2 and desc["clock"]["source"] == "hardware":
        invalid("time_flags", "estimated time cannot be hardware time")
    return channels, count, first, timestamp, host, time_flags, quality_flags


def decode(payload, dtype, channels, count):
    import numpy as np
    if dtype == "int24":
        b = np.frombuffer(payload, dtype=np.uint8).reshape(channels, count, 3).astype(np.int32)
        n = b[:, :, 0] | (b[:, :, 1] << 8) | (b[:, :, 2] << 16)
        samples = (n ^ 0x800000) - 0x800000
    else:
        samples = np.frombuffer(payload, dtype=DTYPES[dtype][1]).reshape(channels, count)
        if dtype.startswith("float") and not np.isfinite(samples).all():
            invalid("payload", "non-finite sample")
    return np.ascontiguousarray(samples.T, dtype=DTYPES[dtype][1])
