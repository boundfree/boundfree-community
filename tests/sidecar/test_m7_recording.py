"""M7 边车录制会话 (``writers/recording.py``) 契约测试。

覆盖参数校验、未知会话、chunk shape/payload 校验、HDF5 数据/元数据/事件、
EDF+HDF5 同时产物、关闭排空与丢弃计数。使用小型确定性 float32 数组。

``pyedflib``/``h5py`` 缺失时仅跳过对应编码验证; 协议与纯逻辑测试不得跳过,
因此纯逻辑测试使用 ``formats=()`` (不触发任何格式依赖的写线程导入)。
"""

import math
from pathlib import Path

import numpy as np
import pytest

from boundfree_sidecar.writers.recording import (
    RecordingRegistry,
    RecordingSession,
    RecordingStats,
)

FS = 256.0
CH_NAMES = ["Fp1", "Fp2"]
N_CH = len(CH_NAMES)


def _chunk(n_samples: int, start: float = 0.0) -> tuple[bytes, tuple[int, int]]:
    """确定性 float32-le chunk: 通道 i 样本 j = start + i*10 + j。"""
    data = np.array(
        [[start + i * 10.0 + j for j in range(n_samples)] for i in range(N_CH)],
        dtype="<f4",
    )
    return data.tobytes(), (N_CH, n_samples)


def _open_session(
    tmp_path: Path,
    *,
    formats: tuple[str, ...] = (),
    max_queue_chunks: int = 256,
    fs: float = FS,
    ch_names: list[str] | None = None,
    meta: dict[str, str] | None = None,
) -> RecordingSession:
    registry = RecordingRegistry()
    session = registry.open(
        fs=fs,
        ch_names=ch_names if ch_names is not None else CH_NAMES,
        output_dir=tmp_path,
        meta=meta if meta is not None else {"animal_id": "rat42", "exp_type": "baseline"},
        formats=formats,
        max_queue_chunks=max_queue_chunks,
    )
    return session


# ---------------------------------------------------------------------------
# 参数校验 (纯逻辑, 不依赖格式编码)
# ---------------------------------------------------------------------------


def test_open_creates_output_dir_and_returns_unique_handle(tmp_path):
    session = _open_session(tmp_path / "deep" / "nested")
    assert session.handle.startswith("record-")
    assert (tmp_path / "deep" / "nested").is_dir()
    session.close(timeout=5.0)


def test_open_refuses_non_directory_output_path(tmp_path):
    not_a_dir = tmp_path / "file.txt"
    not_a_dir.write_text("x", encoding="utf-8")
    registry = RecordingRegistry()
    with pytest.raises(OSError):
        registry.open(
            fs=FS,
            ch_names=CH_NAMES,
            output_dir=not_a_dir,
            meta={"animal_id": "x"},
            formats=(),
        )


# ---------------------------------------------------------------------------
# 未知会话 (纯逻辑)
# ---------------------------------------------------------------------------


def test_registry_get_unknown_handle_returns_none():
    assert RecordingRegistry().get("record-9999") is None


def test_registry_close_unknown_handle_returns_none():
    assert RecordingRegistry().close("record-9999") is None


# ---------------------------------------------------------------------------
# chunk shape / payload 校验 (纯逻辑, formats=())
# ---------------------------------------------------------------------------


def test_write_rejects_wrong_channel_count(tmp_path):
    session = _open_session(tmp_path)
    data = np.zeros((3, 4), dtype="<f4").tobytes()
    with pytest.raises(ValueError, match="invalid recording chunk"):
        session.write(data, (3, 4))
    session.close(timeout=5.0)


def test_write_rejects_zero_samples(tmp_path):
    session = _open_session(tmp_path)
    with pytest.raises(ValueError, match="invalid recording chunk"):
        session.write(b"", (N_CH, 0))
    session.close(timeout=5.0)


def test_write_rejects_payload_length_mismatch(tmp_path):
    session = _open_session(tmp_path)
    # 声称 (2,4) 需要 32 字节, 故意给短 payload
    with pytest.raises(ValueError, match="invalid recording chunk"):
        session.write(b"\x00" * 8, (N_CH, 4))
    session.close(timeout=5.0)


def test_write_rejects_non_finite_samples(tmp_path):
    session = _open_session(tmp_path)
    nan_data = np.full((N_CH, 4), float("nan"), dtype="<f4").tobytes()
    with pytest.raises(ValueError, match="non-finite"):
        session.write(nan_data, (N_CH, 4))
    session.close(timeout=5.0)


# ---------------------------------------------------------------------------
# 事件校验 (纯逻辑)
# ---------------------------------------------------------------------------


def test_event_rejects_negative_time(tmp_path):
    session = _open_session(tmp_path)
    with pytest.raises(ValueError, match="non-negative"):
        session.add_event(-0.5, "bad")
    session.close(timeout=5.0)


def test_event_rejects_non_finite_time(tmp_path):
    session = _open_session(tmp_path)
    with pytest.raises(ValueError, match="non-negative"):
        session.add_event(float("inf"), "bad")
    session.close(timeout=5.0)


# ---------------------------------------------------------------------------
# HDF5 数据 / 元数据 / 事件 (依赖 h5py)
# ---------------------------------------------------------------------------


def test_hdf5_records_data_metadata_and_events(tmp_path):
    h5py = pytest.importorskip("h5py")
    session = _open_session(tmp_path, formats=("hdf5",), meta={"animal_id": "rat42", "exp_type": "baseline"})
    payload, shape = _chunk(4)
    assert session.write(payload, shape) is True
    assert session.add_event(0.5, "stim") is True

    stats = session.close(timeout=5.0)
    assert isinstance(stats, RecordingStats)
    assert stats.samples_written == 4
    assert stats.dropped_chunks == 0
    assert stats.dropped_events == 0

    h5_path = session.paths["hdf5"]
    with h5py.File(h5_path, "r") as f:
        eeg = f["eeg"]
        assert eeg.dtype == np.dtype("<f4")
        assert eeg.shape == (N_CH, 4)
        assert float(eeg.attrs["fs"]) == FS
        assert str(eeg.attrs["unit"]) == "uV"
        names = [n.decode() if isinstance(n, bytes) else str(n) for n in eeg.attrs["ch_names"]]
        assert names == CH_NAMES
        assert f["meta"].attrs["animal_id"] == "rat42"
        assert f["meta"].attrs["exp_type"] == "baseline"
        times = f["events/time"][:]
        labels = f["events/label"][:]
        assert len(times) == len(labels) == 1
        assert math.isfinite(float(times[0])) and float(times[0]) >= 0.0
        assert (labels[0].decode() if isinstance(labels[0], bytes) else labels[0]) == "stim"
        expected = np.array(
            [[i * 10.0 + j for j in range(4)] for i in range(N_CH)], dtype="<f4"
        )
        np.testing.assert_allclose(eeg[:], expected, rtol=0, atol=0)


def test_hdf5_supports_multiple_chunks_appended_along_samples(tmp_path):
    h5py = pytest.importorskip("h5py")
    session = _open_session(tmp_path, formats=("hdf5",))
    p1, s1 = _chunk(3, start=0.0)
    p2, s2 = _chunk(3, start=100.0)
    session.write(p1, s1)
    session.write(p2, s2)
    stats = session.close(timeout=5.0)
    assert stats.samples_written == 6

    with h5py.File(session.paths["hdf5"], "r") as f:
        assert f["eeg"].shape == (N_CH, 6)
        np.testing.assert_allclose(
            f["eeg"][:, 3], np.array([100.0, 110.0], dtype="<f4"), rtol=0, atol=0
        )


# ---------------------------------------------------------------------------
# EDF + HDF5 同时产物
# ---------------------------------------------------------------------------


def test_dual_format_produces_edf_and_hdf5(tmp_path):
    pytest.importorskip("pyedflib")
    h5py = pytest.importorskip("h5py")
    session = _open_session(tmp_path, formats=("edf", "hdf5"))
    payload, shape = _chunk(4)
    session.write(payload, shape)
    session.add_event(0.25, "mark")
    stats = session.close(timeout=5.0)
    assert stats.samples_written == 4

    assert session.paths["edf"].exists()
    assert session.paths["hdf5"].exists()
    assert session.paths["edf"].stat().st_size > 0


def test_edf_records_channel_data(tmp_path):
    pytest.importorskip("pyedflib")
    session = _open_session(tmp_path, formats=("edf",))
    payload, shape = _chunk(5)
    session.write(payload, shape)
    stats = session.close(timeout=5.0)
    assert stats.samples_written == 5

    import mne

    raw = mne.io.read_raw_edf(session.paths["edf"], preload=True, verbose=False)
    assert int(raw.info["sfreq"]) == int(FS)
    assert len(raw.ch_names) == N_CH
    # EDF 固定时长数据记录: 5 样本@256Hz 被填充到整秒 (256 样本) 记录。
    assert raw.n_times >= 5


# ---------------------------------------------------------------------------
# 关闭排空与丢弃计数 (纯逻辑: formats=() 仍排空队列并累计计数, 不写文件)
# ---------------------------------------------------------------------------


def test_close_drains_accepted_queue_before_returning(tmp_path):
    session = _open_session(tmp_path, max_queue_chunks=64)
    for start in (0.0, 10.0, 20.0, 30.0):
        p, s = _chunk(2, start=start)
        session.write(p, s)
    stats = session.close(timeout=5.0)
    assert stats.samples_written == 8
    assert stats.dropped_chunks == 0


def test_full_queue_drops_latest_chunk_and_counts_it(tmp_path):
    """队列满时丢弃最新 chunk (不覆盖已接受数据), 并累计 dropped_chunks。"""
    session = _open_session(tmp_path, max_queue_chunks=1)
    accepted = 0
    for start in (0.0, 10.0, 20.0, 30.0, 40.0):
        p, s = _chunk(2, start=start)
        if session.write(p, s):
            accepted += 1
    stats = session.close(timeout=5.0)
    assert stats.dropped_chunks >= 1
    assert accepted + stats.dropped_chunks == 5
    assert stats.samples_written == accepted * 2


def test_event_dropped_on_full_queue_counted_separately(tmp_path):
    """事件满队列单独累计 dropped_events, 不混入 dropped_chunks。"""
    session = _open_session(tmp_path, max_queue_chunks=1)
    p, s = _chunk(2)
    session.write(p, s)
    dropped = 0
    for _ in range(10):
        if not session.add_event(0.1, "stim"):
            dropped += 1
    stats = session.close(timeout=5.0)
    assert stats.dropped_events == dropped
    assert stats.dropped_events >= 1


def test_close_after_close_returns_not_found_via_registry(tmp_path):
    """重复关闭: 第二次关闭会话已移除, registry.close 返回 None。"""
    registry = RecordingRegistry()
    session = registry.open(
        fs=FS, ch_names=CH_NAMES, output_dir=tmp_path,
        meta={"animal_id": "x"}, formats=(),
    )
    handle = session.handle
    first = registry.close(handle, timeout=5.0)
    assert first is not None
    assert registry.close(handle, timeout=5.0) is None


def test_write_after_close_raises(tmp_path):
    """关闭后继续 write 必须抛出, 不得静默接受。"""
    session = _open_session(tmp_path)
    session.close(timeout=5.0)
    with pytest.raises(RuntimeError):
        session.write(b"\x00" * (N_CH * 4), (N_CH, 1))



# ---------------------------------------------------------------------------
# 协议层 record_* 往返 (handle_request envelope)
# ---------------------------------------------------------------------------
#
# 测试可观察的 IPC 契约: record_open/write/event/close 的成功与错误信封。
# record_open 创建真实会话 (formats=() 不触发格式依赖); write 路径用 hdf5 验证
# 数据真实落盘; 纯校验路径不依赖格式编码。


def _request(method: str, request_id: str, **params) -> dict:
    return {"id": request_id, "method": method, "params": params}


def test_protocol_record_open_validates_missing_params():
    """缺 fs/ch_names/out_dir 返回 protocol.invalid_params 结构化错误。"""
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "ro-1", "method": "record_open", "params": {}})
    assert response["id"] == "ro-1"
    assert response["ok"] is False
    assert response["error"]["code"] == "protocol.invalid_params"
    assert response["error"]["params"]["field"] == "fs"


def test_protocol_record_open_rejects_non_positive_fs(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    response = handle_request(_request(
        "record_open", "ro-fs", fs=0.0, ch_names=CH_NAMES, out_dir=str(tmp_path),
        meta={"animal_id": "x"}, formats=["hdf5"], max_queue_chunks=8,
    ))
    assert response["ok"] is False
    assert response["error"]["code"] == "protocol.invalid_params"
    assert response["error"]["params"]["field"] == "fs"


def test_protocol_record_open_rejects_empty_ch_names(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    response = handle_request(_request(
        "record_open", "ro-ch", fs=FS, ch_names=[], out_dir=str(tmp_path),
        meta={"animal_id": "x"}, formats=["hdf5"], max_queue_chunks=8,
    ))
    assert response["ok"] is False
    assert response["error"]["code"] == "protocol.invalid_params"
    assert response["error"]["params"]["field"] == "ch_names"


def test_protocol_record_open_returns_handle_and_paths(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    response = handle_request(_request(
        "record_open", "ro-ok", fs=FS, ch_names=CH_NAMES, out_dir=str(tmp_path),
        meta={"animal_id": "rat42"}, formats=["hdf5"], max_queue_chunks=8,
    ))
    assert response["ok"] is True
    handle = response["result"]["handle"]
    assert handle.startswith("record-")
    assert "hdf5" in response["result"]["paths"]

    # 清理
    closed = handle_request(_request("record_close", "rc", handle=handle))
    assert closed["ok"] is True


def test_protocol_record_write_unknown_handle_returns_session_not_found():
    from boundfree_sidecar.protocol import handle_request

    payload = np.zeros((N_CH, 2), dtype="<f4").tobytes()
    response = handle_request(
        {"id": "rw-missing", "method": "record_write", "params": {"handle": "record-9999", "shape": [N_CH, 2]}},
        binary_payload=payload,
    )
    assert response["ok"] is False
    assert response["error"]["code"] == "record.session_not_found"
    assert response["error"]["params"]["handle"] == "record-9999"


def test_protocol_record_write_rejects_missing_binary_payload(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    opened = handle_request(_request(
        "record_open", "ro", fs=FS, ch_names=CH_NAMES, out_dir=str(tmp_path),
        meta={"animal_id": "x"}, formats=["hdf5"], max_queue_chunks=8,
    ))
    handle = opened["result"]["handle"]
    try:
        response = handle_request(
            {"id": "rw-nopayload", "method": "record_write",
             "params": {"handle": handle, "shape": [N_CH, 2]}},
            binary_payload=None,
        )
        assert response["ok"] is False
        assert response["error"]["code"] == "record.invalid_chunk"
    finally:
        handle_request(_request("record_close", "rc", handle=handle))


def test_protocol_record_write_rejects_invalid_shape(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    opened = handle_request(_request(
        "record_open", "ro", fs=FS, ch_names=CH_NAMES, out_dir=str(tmp_path),
        meta={"animal_id": "x"}, formats=["hdf5"], max_queue_chunks=8,
    ))
    handle = opened["result"]["handle"]
    try:
        # shape 含零 -> 非法
        response = handle_request(
            {"id": "rw-shape", "method": "record_write",
             "params": {"handle": handle, "shape": [N_CH, 0]}},
            binary_payload=b"\x00" * (N_CH * 4),
        )
        assert response["ok"] is False
        assert response["error"]["code"] == "record.invalid_chunk"
    finally:
        handle_request(_request("record_close", "rc", handle=handle))


def test_protocol_record_write_round_trip_and_stats(tmp_path):
    """record_open -> record_write -> record_close 经 IPC 信封往返; stats 字段存在。"""
    from boundfree_sidecar.protocol import handle_request

    h5py = pytest.importorskip("h5py")
    opened = handle_request(_request(
        "record_open", "ro", fs=FS, ch_names=CH_NAMES, out_dir=str(tmp_path),
        meta={"animal_id": "rat42"}, formats=["hdf5"], max_queue_chunks=8,
    ))
    handle = opened["result"]["handle"]
    payload, shape = _chunk(4)
    try:
        write_resp = handle_request(
            {"id": "rw", "method": "record_write",
             "params": {"handle": handle, "shape": list(shape)}},
            binary_payload=payload,
        )
        assert write_resp["ok"] is True
        assert write_resp["result"]["accepted"] is True
        # 写线程异步消费: write 响应里的 samples_written 不保证已落盘,
        # 仅校验 stats 字段存在且 accepted=True; 落盘计数在 close 排空后断言。
        assert "samples_written" in write_resp["result"]
        assert "backlog_chunks" in write_resp["result"]
        assert "dropped_chunks" in write_resp["result"]
        assert "dropped_events" in write_resp["result"]

        event_resp = handle_request(_request(
            "record_event", "re", handle=handle, time=0.1, label="stim",
        ))
        assert event_resp["ok"] is True
        assert event_resp["result"]["accepted"] is True
    finally:
        closed = handle_request(_request("record_close", "rc", handle=handle))
    assert closed["ok"] is True
    assert closed["result"]["samples_written"] == 4
    assert closed["result"]["dropped_chunks"] == 0
    # HDF5 真实落盘
    with h5py.File(opened["result"]["paths"]["hdf5"], "r") as f:
        assert f["eeg"].shape == (N_CH, 4)


def test_protocol_record_event_unknown_handle_returns_session_not_found():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request(_request(
        "record_event", "re-missing", handle="record-9999", time=0.1, label="x",
    ))
    assert response["ok"] is False
    assert response["error"]["code"] == "record.session_not_found"


def test_protocol_record_event_rejects_invalid_event_params(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    opened = handle_request(_request(
        "record_open", "ro", fs=FS, ch_names=CH_NAMES, out_dir=str(tmp_path),
        meta={"animal_id": "x"}, formats=["hdf5"], max_queue_chunks=8,
    ))
    handle = opened["result"]["handle"]
    try:
        # 负时间
        resp = handle_request(_request("record_event", "re-neg", handle=handle, time=-1.0, label="x"))
        assert resp["ok"] is False
        assert resp["error"]["code"] == "protocol.invalid_params"
    finally:
        handle_request(_request("record_close", "rc", handle=handle))


def test_protocol_record_close_unknown_handle_returns_session_not_found():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request(_request("record_close", "rc-missing", handle="record-9999"))
    assert response["ok"] is False
    assert response["error"]["code"] == "record.session_not_found"
    assert response["error"]["params"]["handle"] == "record-9999"


def test_protocol_record_close_after_close_returns_session_not_found(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    opened = handle_request(_request(
        "record_open", "ro", fs=FS, ch_names=CH_NAMES, out_dir=str(tmp_path),
        meta={"animal_id": "x"}, formats=["hdf5"], max_queue_chunks=8,
    ))
    handle = opened["result"]["handle"]
    first = handle_request(_request("record_close", "rc-1", handle=handle))
    assert first["ok"] is True
    # 重复关闭: 会话已移除
    second = handle_request(_request("record_close", "rc-2", handle=handle))
    assert second["ok"] is False
    assert second["error"]["code"] == "record.session_not_found"