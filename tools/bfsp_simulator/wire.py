"""BFSP 1.0 framing for the simulator; no dependency on the native codec."""

from dataclasses import dataclass
import json
import struct
import time

MAGIC = b"BFSP"
HEADER = struct.Struct("<4sBBHHHIII")
TRAILER = struct.Struct("<I")
DATA_PREFIX = struct.Struct("<IBBHIIQQIIQ")
MAX_PAYLOAD = 1_048_576
MAX_JSON = 65_536

HELLO, HELLO_ACK, COMMAND, RESPONSE, DESCRIPTOR, DATA, EVENT, STATUS, HEARTBEAT, STREAM_END = range(1, 11)
DTYPES = {"int16": (1, 2), "int24": (2, 3), "int32": (3, 4), "float32": (4, 4), "float64": (5, 8)}


def _make_crc_table():
    table = []
    for value in range(256):
        for _ in range(8):
            value = (value >> 1) ^ (0x82F63B78 if value & 1 else 0)
        table.append(value)
    return tuple(table)


_CRC_TABLE = _make_crc_table()


def crc32c(data):
    value = 0xFFFFFFFF
    for byte in data:
        value = _CRC_TABLE[(value ^ byte) & 0xFF] ^ (value >> 8)
    return value ^ 0xFFFFFFFF


def _reject_constant(value):
    raise ValueError("non-finite JSON constant: " + value)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def _check_json(value, depth=1):
    if depth > 16:
        raise ValueError("JSON depth exceeded")
    if isinstance(value, str):
        if len(value.encode("utf-8")) > 4096:
            raise ValueError("JSON string too long")
    elif isinstance(value, list):
        if len(value) > 4096:
            raise ValueError("JSON array too long")
        for item in value:
            _check_json(item, depth + 1)
    elif isinstance(value, dict):
        for key, item in value.items():
            _check_json(key, depth + 1)
            _check_json(item, depth + 1)
    elif isinstance(value, float):
        import math
        if not math.isfinite(value):
            raise ValueError("non-finite JSON number")


def parse_json(payload, limit=MAX_JSON):
    if len(payload) > limit:
        raise ValueError("JSON payload too long")
    try:
        value = json.loads(payload.decode("utf-8"), object_pairs_hook=_unique_object,
                           parse_constant=_reject_constant)
        if not isinstance(value, dict):
            raise ValueError("JSON payload must be an object")
        _check_json(value)
        return value
    except (UnicodeError, RecursionError) as exc:
        raise ValueError("invalid JSON") from exc


def json_bytes(value, limit=MAX_JSON):
    if not isinstance(value, dict):
        raise ValueError("JSON payload must be an object")
    _check_json(value)
    payload = json.dumps(value, ensure_ascii=False, allow_nan=False,
                         separators=(",", ":")).encode("utf-8")
    if len(payload) > limit:
        raise ValueError("JSON payload too long")
    return payload


def encode_frame(kind, stream_id, sequence, payload, limit=MAX_PAYLOAD):
    if kind not in range(1, 11) or len(payload) > min(limit, MAX_PAYLOAD):
        raise ValueError("invalid type or payload length")
    if not (0 <= stream_id <= 0xFFFFFFFF and 0 <= sequence <= 0xFFFFFFFF):
        raise ValueError("invalid stream or sequence")
    header = HEADER.pack(MAGIC, 1, kind, 0, HEADER.size, 0,
                         len(payload), stream_id, sequence)
    frame = header + payload
    return frame + TRAILER.pack(crc32c(frame))


@dataclass(frozen=True)
class Frame:
    kind: int
    stream_id: int
    sequence: int
    payload: bytes


class FrameError(ValueError):
    pass


class VersionMismatch(FrameError):
    pass


class StreamParser:
    """Bounded incremental parser; candidate timeout starts at its first byte."""

    def __init__(self, max_payload=MAX_PAYLOAD, frame_timeout=3.0):
        self.max_payload = min(max_payload, MAX_PAYLOAD)
        self.frame_timeout = frame_timeout
        self.buffer = bytearray()
        self.candidate_since = None
        self.bad = 0
        self.timeouts = 0

    def feed(self, data=b"", now=None):
        now = time.monotonic() if now is None else now
        # A caller supplies bounded chunks. This bound includes an incomplete
        # maximum frame plus one read; do not allocate from an untrusted length.
        if len(data) > 65536 or len(self.buffer) + len(data) > self.max_payload + 28 + 65536:
            raise FrameError("input buffer limit")
        self.buffer.extend(data)
        frames = []
        while self.buffer:
            start = self.buffer.find(MAGIC)
            if start < 0:
                del self.buffer[:-3]
                if not self.buffer or not MAGIC.startswith(self.buffer):
                    # Keep only a suffix that can actually become the magic.
                    while self.buffer and not MAGIC.startswith(self.buffer):
                        del self.buffer[0]
                if self.buffer and self.candidate_since is None:
                    self.candidate_since = now
                if not self.buffer:
                    self.candidate_since = None
                elif now - self.candidate_since >= self.frame_timeout:
                    del self.buffer[0]
                    self.candidate_since = None
                    self.timeouts += 1
                    if self.timeouts >= 3:
                        raise FrameError("too many frame timeouts")
                    continue
                break
            if start:
                del self.buffer[:start]
                self.candidate_since = now
            elif self.candidate_since is None:
                self.candidate_since = now
            if now - self.candidate_since >= self.frame_timeout:
                del self.buffer[0]
                self.candidate_since = None
                self.timeouts += 1
                if self.timeouts >= 3:
                    raise FrameError("too many frame timeouts")
                continue
            if len(self.buffer) < HEADER.size:
                break
            magic, major, kind, flags, head_len, reserved, length, stream, sequence = HEADER.unpack_from(self.buffer)
            if major != 1:
                raise VersionMismatch("unsupported BFSP major")
            if kind not in range(1, 11) or flags or reserved or head_len != 24 or length > self.max_payload:
                self._bad_candidate()
                continue
            size = HEADER.size + length + TRAILER.size
            if len(self.buffer) < size:
                break
            frame_bytes = self.buffer[:size]
            if crc32c(frame_bytes[:-4]) != TRAILER.unpack_from(frame_bytes, size - 4)[0]:
                self._bad_candidate()
                continue
            frames.append(Frame(kind, stream, sequence, bytes(frame_bytes[24:-4])))
            del self.buffer[:size]
            self.candidate_since = None
            self.bad = 0
            self.timeouts = 0
        return frames

    def _bad_candidate(self):
        del self.buffer[0]
        self.candidate_since = None
        self.bad += 1
        if self.bad >= 8:
            raise FrameError("too many invalid frames")
