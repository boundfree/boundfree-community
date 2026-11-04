"""Round-trip the actual recording writer through the ordinary file opener."""
import hashlib
from pathlib import Path
import shutil
import numpy as np
import h5py
import pytest
from boundfree_sidecar.protocol import handle_request
from test_capture_io import descriptor as desc, opened, append, finalize, summary


def replay(path, stream=""):
    return handle_request({"id": "open", "method": "open", "params": {"path": str(path), "capture_stream": stream}})


def sliced(handle, t0, t1):
    return handle_request({"id": "slice", "method": "get_slice", "params": {"handle": handle, "t0": t0, "t1": t1}})


def recording(tmp_path, dtype="int24", origin=0, gap=False):
    d = desc(dtype, channels=2); d["rate_num"] = 250; d["record_start_sample_index"] = str(origin)
    for c in d["channels"]: c.update(unit="count", scale=1, offset=0)
    o = opened(tmp_path, [d]); assert append(o["handle"], d, [[-10, 20], [30, -40]], first=origin)["ok"]
    end = origin + 2
    if gap:
        assert append(o["handle"], d, [[50, 60], [70, 80]], first=origin + 6)["ok"]
        end = origin + 8
    assert finalize(o["handle"], [summary(d, end)])["ok"]
    return Path(o["path"]), d


@pytest.mark.parametrize("dtype", ["int16", "int24", "int32", "float32", "float64"])
def test_recording_opens_from_folder_manifest_or_signals_readonly(tmp_path, dtype):
    path, d = recording(tmp_path, dtype, origin=9007199254741001)
    original = {p.name: hashlib.sha256(p.read_bytes()).digest() for p in path.iterdir()}
    for source in (path, path / "manifest.json", path / "signals.h5"):
        r = replay(source); assert r["ok"], r
        m = r["result"]; assert m["n_samples"] == 2 and m["unit"] == "count" and m["duration"] == .008
        assert m["capture"]["record_start_sample_index"] == d["record_start_sample_index"]
        s = sliced(m["handle"], 0, .008); assert s["ok"], s
        assert s["result"]["data"] == [[-10, 20], [30, -40]]
        assert s["result"]["sample_times"] == [0, .004]
        assert s["result"]["channel_units"] == ["count", "count"]
        handle_request({"id": "close", "method": "close", "params": {"handle": m["handle"]}})
    assert original == {p.name: hashlib.sha256(p.read_bytes()).digest() for p in path.iterdir()}


def test_known_gap_keeps_time_without_zero_fill(tmp_path):
    path, _ = recording(tmp_path, gap=True)
    m = replay(path)["result"]; assert m["duration"] == .032 and m["n_samples"] == 4
    s = sliced(m["handle"], 0, m["duration"]); assert s["ok"], s
    a = np.asarray(s["result"]["data"])
    assert a.shape == (2, 5) and np.isnan(a[:, 2]).all()
    assert a[0, [0, 1, 3, 4]].tolist() == [-10, 20, 50, 60]
    assert s["result"]["sample_times"] == [0, .004, .024, .024, .028]


def test_multiple_streams_keep_units_rate_and_calibration_separate(tmp_path):
    first, second = desc("int16", stream_id=1, channels=1), desc("float64", stream_id=2, channels=1)
    first["rate_num"] = 250; second["rate_num"] = 25
    second["channels"][0].update(unit="degC", scale=.25, offset=10)
    o = opened(tmp_path, [first, second])
    assert append(o["handle"], first, [[10, 20]])["ok"]
    assert append(o["handle"], second, [[40, 80]])["ok"]
    assert finalize(o["handle"], [summary(first, 2), summary(second, 2)])["ok"]
    m = replay(o["path"])["result"]; assert len(m["capture"]["streams"]) == 2
    key = m["capture"]["streams"][1]["key"]
    r = replay(o["path"], key)["result"]; assert r["unit"] == "degC" and r["fs"] == 25
    s = sliced(r["handle"], 0, .08)["result"]; assert s["data"] == [[20, 30]]


def test_corrupt_block_is_not_displayed(tmp_path):
    path, d = recording(tmp_path)
    with h5py.File(path / "signals.h5", "r+") as f:
        f[f"streams/{d['session_id']}/{d['stream_id']}/samples"][0, 0] = 123
    m = replay(path)["result"]
    r = sliced(m["handle"], 0, .008)
    assert not r["ok"] and r["error"]["code"] == "reader.capture_corrupt_block"


def test_missing_manifest_and_unfinished_recording_have_specific_errors(tmp_path):
    path, _ = recording(tmp_path)
    orphan = tmp_path / "orphan.h5"; shutil.copy2(path / "signals.h5", orphan)
    assert replay(orphan)["error"]["code"] == "reader.capture_missing_manifest"
    d = desc(); o = opened(tmp_path, [d])
    try:
        assert replay(o["path"])["error"]["code"] == "reader.capture_incomplete"
    finally:
        finalize(o["handle"], [summary(d, 0)])


def test_external_storage_and_oversized_window_are_rejected(tmp_path, monkeypatch):
    path, d = recording(tmp_path)
    from boundfree_sidecar.readers import capture_reader
    monkeypatch.setattr(capture_reader, "MAX_DISPLAY_BYTES", 1)
    m = replay(path)["result"]
    assert sliced(m["handle"], 0, .008)["error"]["code"] == "reader.capture_slice_too_large"
    with h5py.File(path / "signals.h5", "r+") as f:
        g = f[f"streams/{d['session_id']}/{d['stream_id']}"]
        del g["samples"]; g["samples"] = h5py.ExternalLink("not-opened.h5", "/eeg")
    assert replay(path)["error"]["code"] == "reader.capture_invalid"
