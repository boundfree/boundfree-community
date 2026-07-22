"""M7 边车二进制帧协议 (`framing.py`) 往返与错误契约测试。

帧格式: ``u8 frame_type + u32le payload_length + payload``。
``frame_type=1`` 为 UTF-8 JSON，``frame_type=2`` 为原始二进制 (float32-le EEG)。
本文件只测 ``framing`` 模块的可观察契约: 往返、连续帧、短头、短 payload、
未知类型、超限、小端长度与空 payload。不读取或断言生产源码文本。
"""

import io
import struct

import pytest

from boundfree_sidecar import framing
from boundfree_sidecar.framing import (
    FRAME_BINARY,
    FRAME_JSON,
    MAX_PAYLOAD_BYTES,
    FrameError,
    read_frame,
    read_json_frame,
    write_frame,
    write_json_frame,
)

_HEADER = struct.Struct("<BI")


# ---------------------------------------------------------------------------
# 往返: JSON / 二进制 / 空 payload / 连续帧
# ---------------------------------------------------------------------------


def test_json_frame_round_trips_through_bytes():
    """write_frame(JSON) 再 read_frame 还原 frame_type 与 payload 字节。"""
    payload = '{"id":"r1","ok":true}'.encode("utf-8")
    buf = io.BytesIO()
    write_frame(buf, FRAME_JSON, payload)

    buf.seek(0)
    frame = read_frame(buf)
    assert frame is not None
    assert frame.frame_type == FRAME_JSON
    assert frame.payload == payload


def test_binary_frame_round_trips_preserving_raw_bytes():
    """二进制帧 payload 逐字节保持不变; 不被 JSON 解码。"""
    payload = bytes(range(256))
    buf = io.BytesIO()
    write_frame(buf, FRAME_BINARY, payload)

    buf.seek(0)
    frame = read_frame(buf)
    assert frame is not None
    assert frame.frame_type == FRAME_BINARY
    assert frame.payload == payload


def test_empty_payload_round_trips():
    """长度为 0 的 payload 合法, 不与短帧混淆。"""
    buf = io.BytesIO()
    write_frame(buf, FRAME_JSON, b"")

    buf.seek(0)
    frame = read_frame(buf)
    assert frame is not None
    assert frame.frame_type == FRAME_JSON
    assert frame.payload == b""


def test_multiple_continuous_frames_round_trip_in_order():
    """同一流中连续多帧按写入顺序逐一读回, 验证无残留字节错位。"""
    frames = [
        (FRAME_JSON, b'{"method":"hello"}'),
        (FRAME_BINARY, b"\x00\x00\x80\x3f"),  # float32 1.0
        (FRAME_JSON, b'{"ok":true}'),
        (FRAME_BINARY, b""),
    ]
    buf = io.BytesIO()
    for frame_type, payload in frames:
        write_frame(buf, frame_type, payload)

    buf.seek(0)
    for frame_type, payload in frames:
        frame = read_frame(buf)
        assert frame is not None
        assert (frame.frame_type, frame.payload) == (frame_type, payload)

    # 流应正好耗尽: 再读得到 None (干净 EOF)。
    assert read_frame(buf) is None


def test_json_metadata_then_binary_data_sequence_round_trips():
    """get_slice 响应契约: JSON 元数据帧后跟一个 float32-le 二进制数据帧。"""
    import numpy as np

    data = np.array([[1.0, 2.0, 3.0], [10.0, 20.0, 30.0]], dtype="<f4")
    meta = {"shape": [2, 3], "unit": "uV"}
    buf = io.BytesIO()
    write_json_frame(buf, meta)
    write_frame(buf, FRAME_BINARY, data.tobytes())

    buf.seek(0)
    meta_back = read_json_frame(buf)
    data_frame = read_frame(buf)
    assert meta_back == meta
    assert data_frame is not None
    assert data_frame.frame_type == FRAME_BINARY
    np.testing.assert_array_equal(
        np.frombuffer(data_frame.payload, dtype="<f4").reshape(2, 3), data
    )


# ---------------------------------------------------------------------------
# read_json_frame / write_json_frame
# ---------------------------------------------------------------------------


def test_write_json_frame_round_trips_unicode_payload():
    """write_json_frame 编码为 UTF-8 JSON, read_json_frame 解码还原含非 ASCII。"""
    value = {"method": "未知方法", "params": {"label": "标记Æ"}}
    buf = io.BytesIO()
    write_json_frame(buf, value)

    buf.seek(0)
    assert read_json_frame(buf) == value


def test_read_json_frame_rejects_binary_frame():
    """对二进制帧调用 read_json_frame 返回 FrameError 而非乱码。"""
    buf = io.BytesIO()
    write_frame(buf, FRAME_BINARY, b"\x00\x00\x80\x3f")

    buf.seek(0)
    with pytest.raises(FrameError):
        read_json_frame(buf)


def test_read_frame_returns_none_on_clean_eof():
    """空流 (无任何字节) 返回 None, 不抛短帧错误。"""
    assert read_frame(io.BytesIO()) is None


# ---------------------------------------------------------------------------
# 错误: 短头 / 短 payload / 未知类型 / 超限 / 大端长度
# ---------------------------------------------------------------------------


def test_short_header_raises_frame_error():
    """header 不足 5 字节 (仅读到类型字节后 EOF) 报短帧。"""
    buf = io.BytesIO(b"\x01\x00\x00")  # 类型 + 2 字节长度后 EOF
    with pytest.raises(FrameError, match="short frame"):
        read_frame(buf)


def test_short_payload_raises_frame_error():
    """header 声称 payload 较长但实际字节不足报短帧。"""
    header = _HEADER.pack(FRAME_JSON, 10)
    buf = io.BytesIO(header + b"abc")
    with pytest.raises(FrameError, match="short frame"):
        read_frame(buf)


@pytest.mark.parametrize("bad_type", [0, 3, 9, 255])
def test_unknown_frame_type_raises_frame_error(bad_type):
    """frame_type 不在 {1,2} 报未知类型; read 与 write 都拒绝。"""
    buf = io.BytesIO(_HEADER.pack(bad_type, 0))
    with pytest.raises(FrameError, match="unknown frame type"):
        read_frame(buf)

    with pytest.raises(FrameError, match="unknown frame type"):
        write_frame(io.BytesIO(), bad_type, b"")


def test_read_frame_rejects_payload_exceeding_default_limit():
    """payload_length 超过 MAX_PAYLOAD_BYTES 在读前即拒绝, 不读巨量字节。"""
    buf = io.BytesIO(_HEADER.pack(FRAME_JSON, MAX_PAYLOAD_BYTES + 1))
    with pytest.raises(FrameError, match="exceeds limit"):
        read_frame(buf)


def test_read_frame_rejects_payload_exceeding_custom_limit():
    """自定义 max_payload_bytes 上限同样生效。"""
    buf = io.BytesIO(_HEADER.pack(FRAME_JSON, 1024))
    with pytest.raises(FrameError, match="exceeds limit"):
        read_frame(buf, max_payload_bytes=512)


def test_write_frame_rejects_payload_exceeding_limit():
    """write_frame 对超过上限的 payload 在写前即拒绝。"""
    payload = b"\x00" * (MAX_PAYLOAD_BYTES + 1)
    with pytest.raises(FrameError, match="exceeds limit"):
        write_frame(io.BytesIO(), FRAME_JSON, payload)


# ---------------------------------------------------------------------------
# 小端长度编码契约
# ---------------------------------------------------------------------------


def test_payload_length_is_little_endian_on_the_wire():
    """小端长度: 手工小端 header 解码还原 payload; 大端长度被误读后必须被检出。

    防御帧头字节序回归: 一旦误用大端, 长度会错位, read_frame 必须拒绝。
    """
    # 小端: 长度 5 -> bytes 05 00 00 00
    buf = io.BytesIO(bytes([FRAME_JSON]) + struct.pack("<I", 5) + b"hello")
    frame = read_frame(buf)
    assert frame is not None
    assert frame.payload == b"hello"

    # 大端: 长度 5 -> bytes 00 00 00 05; 作为小端读取得到 0x05000000 (< 256 MiB)。
    # 该值通过上限检查, 但实际字节不足, 报短帧: 字节序回归仍被检出为错误。
    bad_buf = io.BytesIO(bytes([FRAME_JSON]) + struct.pack(">I", 5) + b"hello")
    with pytest.raises(FrameError):
        read_frame(bad_buf)

def test_header_is_exactly_five_bytes_little_endian():
    """帧头固定 5 字节: 1 字节类型 + 4 字节小端长度。"""
    buf = io.BytesIO()
    write_frame(buf, FRAME_JSON, b"x")  # 1 字节 payload
    raw = buf.getvalue()
    assert len(raw) == 5 + 1
    frame_type, length = _HEADER.unpack(raw[:5])
    assert frame_type == FRAME_JSON
    assert length == 1
