"""M7 边车片段导出 (``writers/export.py``) 契约测试。

覆盖 CSV ``time_s``+marker 侧车、HDF5 events、EDF annotation、区间校验、
扩展名错误和重新打开后的数据/标注语义。使用小型确定性源与 float32 容差比较。
``pyedflib`` 缺失时仅跳过 EDF 编码验证; CSV/HDF5 与纯逻辑测试不得跳过。
"""

import csv
import math
from pathlib import Path

import numpy as np
import pytest

from boundfree_sidecar.readers.registry import reader_for_path
from boundfree_sidecar.writers.export import export_segment

FS = 100.0
N_CH = 2
CH_NAMES = ["ch1", "ch2"]


# ---------------------------------------------------------------------------
# 源 fixture 构造
# ---------------------------------------------------------------------------


def _data_value(channel: int, sample: int) -> float:
    return float(channel * 10 + sample)


def _write_csv_source(path: Path, n_samples: int = 10) -> None:
    """100Hz, 2 通道, n_samples 个样本; 通道 i 样本 j = i*10+j。"""
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["time_s", *CH_NAMES])
        for j in range(n_samples):
            writer.writerow([f"{j / FS:.4f}", _data_value(0, j), _data_value(1, j)])


def _write_hdf5_source(path: Path, n_samples: int = 10) -> None:
    h5py = pytest.importorskip("h5py")
    data = np.array(
        [[_data_value(i, j) for j in range(n_samples)] for i in range(N_CH)],
        dtype="<f4",
    )
    with h5py.File(path, "w") as f:
        ds = f.create_dataset("eeg", data=data)
        ds.attrs["fs"] = FS
        ds.attrs["ch_names"] = np.asarray(CH_NAMES, dtype=h5py.string_dtype("utf-8"))
        ds.attrs["unit"] = "uV"


@pytest.fixture()
def csv_source(tmp_path: Path) -> Path:
    path = tmp_path / "src.csv"
    _write_csv_source(path)
    return path


@pytest.fixture()
def hdf5_source(tmp_path: Path) -> Path:
    path = tmp_path / "src.h5"
    _write_hdf5_source(path)
    return path


def _markers(*pairs: tuple[float, str]) -> list[dict[str, object]]:
    return [{"time": t, "label": label} for t, label in pairs]


# ---------------------------------------------------------------------------
# CSV 导出: time_s + marker 侧车
# ---------------------------------------------------------------------------


def test_csv_export_writes_time_s_and_channel_columns(tmp_path: Path, csv_source: Path):
    dest = tmp_path / "out.csv"
    result = export_segment(csv_source, 0.0, 0.05, dest, markers=[])

    assert result["n_channels"] == N_CH
    assert result["n_samples"] == 5
    assert result["marker_count"] == 0
    assert dest.exists()

    with dest.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        header = reader.fieldnames
        assert header == ["time_s", *CH_NAMES]
        rows = list(reader)
    assert len(rows) == 5
    # time_s 相对片段起点 (t0=0): 第 j 行 = j/FS
    assert float(rows[0]["time_s"]) == pytest.approx(0.0)
    assert float(rows[4]["time_s"]) == pytest.approx(0.04)
    # 通道列数值往返
    assert float(rows[2]["ch1"]) == pytest.approx(_data_value(0, 2), abs=1e-5)


def test_csv_export_writes_markers_sidecar_rebased_to_segment_start(tmp_path: Path, csv_source: Path):
    """标注时间相对片段起点: 源标注 0.03 在区间 [0.02, 0.06] 内 -> 0.01。"""
    dest = tmp_path / "seg.csv"
    markers = _markers((0.03, "stim"), (0.09, "out-of-range"), (0.02, "start"))
    result = export_segment(csv_source, 0.02, 0.06, dest, markers=markers)

    assert result["marker_count"] == 2  # 0.03 与 0.02 在区间内
    marker_path = dest.with_name(dest.stem + "_markers.csv")
    assert marker_path.exists()
    with marker_path.open("r", encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    times = [float(r["time_s"]) for r in rows]
    labels = [r["label"] for r in rows]
    assert labels == ["stim", "start"]
    assert times == pytest.approx([0.01, 0.0], abs=1e-6)


def test_csv_export_no_markers_omits_sidecar(tmp_path: Path, csv_source: Path):
    dest = tmp_path / "nomark.csv"
    export_segment(csv_source, 0.0, 0.03, dest, markers=[])
    sidecar = dest.with_name(dest.stem + "_markers.csv")
    assert not sidecar.exists()


# ---------------------------------------------------------------------------
# CSV 导出结果重新打开: 数据与标注语义
# ---------------------------------------------------------------------------


def test_csv_export_reopened_preserves_data_and_events(tmp_path: Path, csv_source: Path):
    """导出 CSV 经 CsvReader 重新打开: fs 推导正确、数据往返、事件回读一致。"""
    dest = tmp_path / "reopen.csv"
    markers = _markers((0.01, "a"), (0.04, "b"))
    export_segment(csv_source, 0.0, 0.05, dest, markers=markers)

    reader = reader_for_path(dest)
    meta = reader.metadata()
    assert meta.fs == pytest.approx(FS, abs=1e-6)
    assert meta.n_channels == N_CH
    assert meta.n_samples == 5
    assert [e["label"] for e in meta.events] == ["a", "b"]
    assert [float(e["time"]) for e in meta.events] == pytest.approx([0.01, 0.04], abs=1e-6)

    sl = reader.get_slice(0.0, 0.03)
    # 样本 0..2: ch1 = 0,1,2 ; ch2 = 10,11,12
    np.testing.assert_allclose(
        np.array(sl.data, dtype=np.float32),
        np.array([[0.0, 1.0, 2.0], [10.0, 11.0, 12.0]], dtype=np.float32),
        atol=1e-4,
    )


# ---------------------------------------------------------------------------
# HDF5 导出: events 与重新打开
# ---------------------------------------------------------------------------


def test_hdf5_export_writes_eeg_events_and_metadata(tmp_path: Path, hdf5_source: Path):
    h5py = pytest.importorskip("h5py")
    dest = tmp_path / "out.h5"
    markers = _markers((0.01, "stim"), (0.05, "end"))
    result = export_segment(hdf5_source, 0.0, 0.06, dest, markers=markers)

    assert result["n_channels"] == N_CH
    assert result["n_samples"] == 6
    assert result["marker_count"] == 2

    with h5py.File(dest, "r") as f:
        eeg = f["eeg"]
        assert eeg.dtype == np.dtype("<f4")
        assert eeg.shape == (N_CH, 6)
        assert float(eeg.attrs["fs"]) == FS
        assert str(eeg.attrs["unit"]) == "uV"
        times = f["events/time"][:]
        labels = f["events/label"][:]
        assert len(times) == len(labels) == 2
        assert [t for t in times] == pytest.approx([0.01, 0.05], abs=1e-6)
        assert [
            lab.decode() if isinstance(lab, bytes) else lab for lab in labels
        ] == ["stim", "end"]


def test_hdf5_export_reopened_preserves_data_and_events(tmp_path: Path, hdf5_source: Path):
    """导出 HDF5 经 Hdf5Reader 重新打开: 数据与 events/time+label 往返一致。"""
    dest = tmp_path / "reopen.h5"
    markers = _markers((0.02, "mark"))
    export_segment(hdf5_source, 0.0, 0.05, dest, markers=markers)

    reader = reader_for_path(dest)
    meta = reader.metadata()
    assert meta.fs == pytest.approx(FS, abs=1e-6)
    assert meta.n_channels == N_CH
    assert meta.n_samples == 5

    sl = reader.get_slice(0.0, 0.03)
    np.testing.assert_allclose(
        np.array(sl.data, dtype=np.float32),
        np.array([[0.0, 1.0, 2.0], [10.0, 11.0, 12.0]], dtype=np.float32),
        atol=1e-5,
    )


def test_hdf5_export_without_markers_omits_events_group(tmp_path: Path, hdf5_source: Path):
    h5py = pytest.importorskip("h5py")
    dest = tmp_path / "noevents.h5"
    export_segment(hdf5_source, 0.0, 0.03, dest, markers=[])
    with h5py.File(dest, "r") as f:
        assert "events" not in f


# ---------------------------------------------------------------------------
# EDF 导出: annotation 与重新打开
# ---------------------------------------------------------------------------


@pytest.fixture()
def edf_source(tmp_path: Path) -> Path:
    """用现有 generated EDF fixture 作为源 (100Hz, 2ch, 100 samples, 1.0s)。"""
    root = Path(__file__).resolve().parents[2]
    return root / "tests" / "fixtures" / "generated" / "simple_eeg.edf"


def test_edf_export_writes_annotations_rebased_to_segment(edf_source: Path, tmp_path: Path):
    pytest.importorskip("pyedflib")
    if not edf_source.exists():
        pytest.skip("EDF fixture missing")
    dest = tmp_path / "out.edf"
    markers = _markers((0.1, "stim"), (0.35, "mid"))
    result = export_segment(edf_source, 0.1, 0.4, dest, markers=markers)

    assert result["n_samples"] == 30
    assert result["marker_count"] == 2

    import mne

    raw = mne.io.read_raw_edf(dest, preload=False, verbose=False)
    assert int(raw.info["sfreq"]) == 100
    # EDF 固定时长数据记录: 子秒片段被填充到整秒记录, 故磁盘样本数 >= 逻辑样本数。
    assert raw.n_times >= result["n_samples"]
    onsets = [float(o) for o in raw.annotations.onset]
    descriptions = [str(d) for d in raw.annotations.description]
    # 契约: 片段内全部标注写入且时间相对片段起点。
    assert "stim" in descriptions and "mid" in descriptions
    # 标注相对片段起点: stim (0.1) -> 0.0, mid (0.35) -> 0.25
    stim_onset = onsets[descriptions.index("stim")]
    mid_onset = onsets[descriptions.index("mid")]
    assert stim_onset == pytest.approx(0.0, abs=1e-3)
    assert mid_onset == pytest.approx(0.25, abs=1e-3)


def test_edf_export_reopened_preserves_channel_count(edf_source: Path, tmp_path: Path):
    pytest.importorskip("pyedflib")
    if not edf_source.exists():
        pytest.skip("EDF fixture missing")
    dest = tmp_path / "reopen.edf"
    export_segment(edf_source, 0.0, 0.2, dest, markers=[])
    import mne

    raw = mne.io.read_raw_edf(dest, preload=False, verbose=False)
    assert len(raw.ch_names) == 2
    # EDF 数据记录填充: 磁盘样本数 >= 逻辑导出样本数 (0.2s@100Hz -> 20 逻辑样本)。
    assert raw.n_times >= 20


# ---------------------------------------------------------------------------
# 区间校验 (纯逻辑, 不依赖格式编码)
# ---------------------------------------------------------------------------


def test_export_rejects_inverted_range(tmp_path: Path, csv_source: Path):
    dest = tmp_path / "x.csv"
    with pytest.raises(ValueError, match="invalid export range"):
        export_segment(csv_source, 0.05, 0.02, dest, markers=[])


def test_export_rejects_range_exceeding_duration(tmp_path: Path, csv_source: Path):
    dest = tmp_path / "x.csv"
    with pytest.raises(ValueError, match="invalid export range"):
        export_segment(csv_source, 0.0, 10.0, dest, markers=[])


def test_export_rejects_negative_t0(tmp_path: Path, csv_source: Path):
    dest = tmp_path / "x.csv"
    with pytest.raises(ValueError, match="invalid export range"):
        export_segment(csv_source, -0.1, 0.05, dest, markers=[])


def test_export_rejects_zero_length_range(tmp_path: Path, csv_source: Path):
    dest = tmp_path / "x.csv"
    with pytest.raises(ValueError, match="invalid export range"):
        export_segment(csv_source, 0.05, 0.05, dest, markers=[])


# ---------------------------------------------------------------------------
# 扩展名错误
# ---------------------------------------------------------------------------


def test_export_rejects_unsupported_extension(tmp_path: Path, csv_source: Path):
    dest = tmp_path / "out.xyz"
    with pytest.raises(ValueError, match="unsupported export format"):
        export_segment(csv_source, 0.0, 0.05, dest, markers=[])


def test_export_rejects_unsupported_source_format(tmp_path: Path):
    bad_source = tmp_path / "src.xyz"
    bad_source.write_text("x", encoding="utf-8")
    dest = tmp_path / "out.csv"
    with pytest.raises(ValueError, match="unsupported source format"):
        export_segment(bad_source, 0.0, 0.05, dest, markers=[])


# ---------------------------------------------------------------------------
# 标注筛选边界: 区间端点包含
# ---------------------------------------------------------------------------


def test_marker_at_range_boundary_is_included(tmp_path: Path, csv_source: Path):
    """t0 <= marker.time <= t1: 端点标注被纳入 (且时间重基准为 0)。"""
    dest = tmp_path / "boundary.csv"
    markers = _markers((0.02, "start-edge"), (0.05, "end-edge"), (0.06, "after"))
    result = export_segment(csv_source, 0.02, 0.05, dest, markers=markers)
    assert result["marker_count"] == 2
    marker_path = dest.with_name(dest.stem + "_markers.csv")
    with marker_path.open("r", encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    labels = [r["label"] for r in rows]
    times = [float(r["time_s"]) for r in rows]
    assert labels == ["start-edge", "end-edge"]
    assert times == pytest.approx([0.0, 0.03], abs=1e-6)



# ---------------------------------------------------------------------------
# 协议层 export_segment 往返 (handle_request envelope)
# ---------------------------------------------------------------------------


def _req(method: str, request_id: str, **params) -> dict:
    return {"id": request_id, "method": method, "params": params}


def test_protocol_export_segment_csv_round_trip(tmp_path: Path, csv_source: Path):
    """export_segment 经 IPC 信封: CSV 成功响应含 path/n_channels/n_samples/marker_count。"""
    from boundfree_sidecar.protocol import handle_request

    dest = tmp_path / "proto_out.csv"
    markers = _markers((0.01, "stim"))
    response = handle_request(_req(
        "export_segment", "es-1", src=str(csv_source), path=str(dest),
        t0=0.0, t1=0.05, markers=markers,
    ))
    assert response["id"] == "es-1"
    assert response["ok"] is True
    result = response["result"]
    assert result["path"] == str(dest)
    assert result["n_channels"] == N_CH
    assert result["n_samples"] == 5
    assert result["marker_count"] == 1
    assert dest.exists()


def test_protocol_export_segment_rejects_unsupported_extension(tmp_path: Path, csv_source: Path):
    from boundfree_sidecar.protocol import handle_request

    dest = tmp_path / "out.xyz"
    response = handle_request(_req(
        "export_segment", "es-ext", src=str(csv_source), path=str(dest),
        t0=0.0, t1=0.05, markers=[],
    ))
    assert response["ok"] is False
    assert response["error"]["code"] == "export.unsupported_format"
    assert response["error"]["params"]["suffix"] == ".xyz"


def test_protocol_export_segment_rejects_invalid_range(tmp_path: Path, csv_source: Path):
    from boundfree_sidecar.protocol import handle_request

    dest = tmp_path / "out.csv"
    response = handle_request(_req(
        "export_segment", "es-range", src=str(csv_source), path=str(dest),
        t0=0.05, t1=0.02, markers=[],
    ))
    assert response["ok"] is False
    assert response["error"]["code"] == "export.invalid_range"


def test_protocol_export_segment_rejects_missing_source(tmp_path: Path, csv_source: Path):
    from boundfree_sidecar.protocol import handle_request

    dest = tmp_path / "out.csv"
    response = handle_request(_req(
        "export_segment", "es-src", src=str(tmp_path / "missing.csv"), path=str(dest),
        t0=0.0, t1=0.05, markers=[],
    ))
    assert response["ok"] is False
    assert response["error"]["code"] == "file.not_found"


def test_protocol_export_segment_hdf5_events_rebased(tmp_path: Path, hdf5_source: Path):
    """HDF5 导出经 IPC 信封: events 写入且时间相对片段起点。"""
    from boundfree_sidecar.protocol import handle_request

    h5py = pytest.importorskip("h5py")
    dest = tmp_path / "proto_out.h5"
    markers = _markers((0.03, "mark"))
    response = handle_request(_req(
        "export_segment", "es-h5", src=str(hdf5_source), path=str(dest),
        t0=0.02, t1=0.06, markers=markers,
    ))
    assert response["ok"] is True
    assert response["result"]["marker_count"] == 1
    with h5py.File(dest, "r") as f:
        times = f["events/time"][:]
        assert float(times[0]) == pytest.approx(0.01, abs=1e-6)


def test_protocol_export_segment_reopened_csv_preserves_semantics(tmp_path: Path, csv_source: Path):
    """导出结果经 open/get_slice 重新打开: 数据与标注语义往返一致。"""
    from boundfree_sidecar.protocol import handle_request

    dest = tmp_path / "reopen_proto.csv"
    markers = _markers((0.02, "a"))
    export_resp = handle_request(_req(
        "export_segment", "es-export", src=str(csv_source), path=str(dest),
        t0=0.0, t1=0.05, markers=markers,
    ))
    assert export_resp["ok"] is True

    opened = handle_request({"id": "open", "method": "open", "params": {"path": str(dest)}})
    assert opened["ok"] is True
    assert opened["result"]["n_channels"] == N_CH
    assert opened["result"]["n_samples"] == 5
    assert [e["label"] for e in opened["result"]["events"]] == ["a"]
    assert [float(e["time"]) for e in opened["result"]["events"]] == pytest.approx([0.02], abs=1e-6)

    handle = opened["result"]["handle"]
    sl = handle_request({"id": "gs", "method": "get_slice",
                         "params": {"handle": handle, "t0": 0.0, "t1": 0.03}})
    assert sl["ok"] is True
    assert sl["result"]["shape"] == [N_CH, 3]
    # 清理会话
    handle_request({"id": "cl", "method": "close", "params": {"handle": handle}})