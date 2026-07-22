import json
import struct
from dataclasses import dataclass
from typing import BinaryIO, Any


FRAME_JSON = 1
FRAME_BINARY = 2
MAX_PAYLOAD_BYTES = 256 * 1024 * 1024
_HEADER = struct.Struct("<BI")


class FrameError(ValueError):
    pass


@dataclass(frozen=True)
class Frame:
    frame_type: int
    payload: bytes


def _read_exact(stream: BinaryIO, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = stream.read(remaining)
        if not chunk:
            raise FrameError(f"short frame: expected {size} bytes")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_frame(stream: BinaryIO, max_payload_bytes: int = MAX_PAYLOAD_BYTES) -> Frame | None:
    first = stream.read(1)
    if not first:
        return None
    rest = _read_exact(stream, _HEADER.size - 1)
    frame_type, payload_length = _HEADER.unpack(first + rest)
    if frame_type not in (FRAME_JSON, FRAME_BINARY):
        raise FrameError(f"unknown frame type {frame_type}")
    if payload_length > max_payload_bytes:
        raise FrameError(f"payload length {payload_length} exceeds limit {max_payload_bytes}")
    return Frame(frame_type, _read_exact(stream, payload_length))


def write_frame(stream: BinaryIO, frame_type: int, payload: bytes) -> None:
    if frame_type not in (FRAME_JSON, FRAME_BINARY):
        raise FrameError(f"unknown frame type {frame_type}")
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise FrameError(f"payload length {len(payload)} exceeds limit {MAX_PAYLOAD_BYTES}")
    stream.write(_HEADER.pack(frame_type, len(payload)))
    stream.write(payload)
    stream.flush()


def read_json_frame(stream: BinaryIO) -> Any | None:
    frame = read_frame(stream)
    if frame is None:
        return None
    if frame.frame_type != FRAME_JSON:
        raise FrameError("expected JSON frame")
    try:
        return json.loads(frame.payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise FrameError("invalid JSON payload") from exc


def write_json_frame(stream: BinaryIO, value: Any) -> None:
    payload = json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    write_frame(stream, FRAME_JSON, payload)
