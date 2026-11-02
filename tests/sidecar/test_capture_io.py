import errno
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from uuid import uuid4

import h5py
import numpy as np
import pytest

from boundfree_sidecar import capture_io
from boundfree_sidecar.protocol import handle_request


@pytest.fixture(autouse=True)
def close_captures():
    yield
    capture_io.cleanup()


def ident(d):
    return {k: d[k] for k in ("device_id", "boot_id", "session_id", "capture_id", "generation", "stream_id", "descriptor_revision")}


def descriptor(dtype="int24", stream_id=1, session_id=None, channels=2):
    return {
        "device_id": "device-1", "boot_id": str(uuid4()), "session_id": session_id or str(uuid4()),
        "capture_id": str(uuid4()), "generation": "9007199254740993", "stream_id": stream_id,
        "descriptor_revision": 1, "name": "raw", "signal_type": "eeg", "rate_num": 1000, "rate_den": 1,
        "dtype": dtype, "layout": "channel_major", "max_samples_per_block": 1000,
        "channels": [{"channel_id": f"ch{i}", "name": f"Channel {i}", "signal_type": "eeg",
                      "unit": "uV", "scale": 0.1, "offset": 0} for i in range(channels)],
        "clock": {"clock_id": 3, "ticks_per_second_num": "1000000", "ticks_per_second_den": "1",
                  "source": "hardware", "epoch": "boot_relative"},
    }


def request(method, params=None, payload=None):
    return handle_request({"id": "id-1", "method": method, "params": params or {}}, payload)


def opened(tmp_path, descriptors):
    response = request("capture_open", {"out_dir": str(tmp_path),
        "host_clock": {"source": "monotonic", "boot_id": "host-boot", "monotonic_base_ns": "9007199254740993"},
        "streams": descriptors})
    assert response["ok"], response
    return response["result"]


def encoded(values, dtype):
    if dtype == "int24":
        unsigned = np.asarray(values, dtype=np.int32).astype(np.uint32) & 0xffffff
        return np.stack([unsigned & 255, (unsigned >> 8) & 255, (unsigned >> 16) & 255], axis=-1).astype("u1").tobytes()
    return np.asarray(values, dtype={"int16": "<i2", "int32": "<i4", "float32": "<f4", "float64": "<f8"}[dtype]).tobytes()


def append(handle, d, values, first=0, **changes):
    shape = list(np.asarray(values).shape)
    params = {"handle": handle, "stream": ident(d), "dtype": d["dtype"], "shape": shape,
              "sample_count": shape[1], "first_sample_index": str(first), "first_timestamp_ticks": "1000",
              "host_received_monotonic_ns": "9007199254740994", "clock_id": 3, "time_flags": 1, "quality_flags": 0}
    params.update(changes)
    return request("capture_append", params, encoded(values, d["dtype"]))


def summary(d, last, terminal=None, complete=True, **kwargs):
    return {"stream": ident(d), "last_accepted_sample_exclusive": str(last),
            "terminal_sample_exclusive": str(last if terminal is None else terminal), "complete": complete, **kwargs}


def finalize(handle, summaries):
    return request("capture_finalize", {"handle": handle, "streams": summaries})


def inspect(path):
    response = request("capture_inspect", {"path": str(path)})
    assert response["ok"], response
    return response["result"]


@pytest.mark.parametrize("requested,advertised", [
    (["capture.v1"], ["capture.v1"]),
    (["capture.metadata.v1"], ["capture.metadata.v1"]),
    (["capture.metadata.v1", "capture.v1"], ["capture.v1", "capture.metadata.v1"]),
    (["capture.v1", "capture.v1", "capture.metadata.v1", "unknown"], ["capture.v1", "capture.metadata.v1"]),
])
def test_hello_advertises_only_requested_capture_capabilities(requested, advertised):
    original = request("hello")["result"]
    assert "capabilities" not in original
    response = request("hello", {"requested_capabilities": requested})
    assert response["result"] == {**original, "capabilities": advertised}
    assert request("hello", {"requested_capabilities": ["unknown"]})["result"] == original


@pytest.mark.parametrize("requested", [["capture.v1"], ["capture.metadata.v1"],
                                        ["capture.v1", "capture.metadata.v1"]])
def test_hello_capture_capabilities_unavailable_when_dependencies_missing(monkeypatch, requested):
    monkeypatch.setattr("boundfree_sidecar.protocol.capture_available", lambda: False)
    original = request("hello")["result"]
    assert request("hello", {"requested_capabilities": requested})["result"] == {**original, "capabilities": []}


@pytest.mark.parametrize("dtype,values", [
    ("int16", [[-32768, -1, 32767], [0, 5, -30000]]),
    ("int24", [[-8388608, -1, 8388607], [0, 128, -8388607]]),
    ("int32", [[-2147483648, -1, 2147483647], [0, 16777217, -16777217]]),
    ("float32", [[-0.0, 1.0000001192092896, 1e-38], [3.25, -12.5, 1e20]]),
    ("float64", [[-0.0, 1.0000000000000002, 1e-300], [3.25, -12.5, 1e200]]),
])
def test_raw_types_round_trip_without_precision_loss(tmp_path, dtype, values):
    d = descriptor(dtype)
    result = opened(tmp_path, [d])
    path = Path(result["path"])
    assert json.loads((path / "manifest.json").read_text())["finalized"] is False
    written = append(result["handle"], d, values)
    assert written["ok"] and written["result"]["written_samples"] == "3"
    assert written["result"]["durable_samples"] is None
    done = finalize(result["handle"], [summary(d, 3)])
    assert done["ok"] and done["result"]["status"] == "complete"
    assert done["result"]["streams"][0]["flushed_samples"] == "3"
    with h5py.File(path / "signals.h5", "r") as h5:
        group = h5[f'streams/{d["session_id"]}/1']
        expected = np.asarray(values, dtype={"int16": "<i2", "int24": "<i4", "int32": "<i4",
                                               "float32": "<f4", "float64": "<f8"}[dtype]).T.copy()
        assert group["samples"][:].tobytes() == expected.tobytes()
        assert group["samples"].dtype == expected.dtype
        assert group["blocks"][0]["sample_index"] == 0
    manifest = json.loads((path / "manifest.json").read_text())
    assert manifest["finalized"] is True
    assert manifest["streams"][0]["valid_bits"] == (24 if dtype == "int24" else None)
    checked = inspect(path)
    assert checked["verification"] == "ok" and checked["streams"][0]["verified_samples"] == "3"
    assert "acquisition_metadata" not in manifest and "acquisition_metadata" not in checked


@pytest.mark.parametrize("ending", ["complete", "partial", "interrupted"])
def test_acquisition_metadata_round_trip_and_snapshot(tmp_path, ending):
    d = descriptor("int16")
    metadata = {"rate_requested": "250", "rate_readback": "unknown", "gain_origin": "declared",
                "vref_origin": "unknown", "start_boundary": "unknown", "说明": "仅声明，未读回",
                "\ufeffrate": "\ufeff250"}
    expected = dict(metadata)
    params = {"out_dir": str(tmp_path), "host_clock": {"source": "monotonic", "boot_id": "host",
              "monotonic_base_ns": "0"}, "streams": [d], "acquisition_metadata": metadata}
    response = request("capture_open", params)
    assert response["ok"], response
    capture = response["result"]
    path = Path(capture["path"])
    metadata["rate_readback"] = "changed after open"
    assert json.loads((path / "manifest.json").read_text(encoding="utf-8"))["acquisition_metadata"] == expected
    assert inspect(path)["acquisition_metadata"] == expected
    assert append(capture["handle"], d, [[1], [2]])["ok"]
    if ending == "interrupted":
        capture_io.cleanup()
    else:
        end = summary(d, 1, complete=ending == "complete")
        if ending == "partial":
            end["terminal_sample_exclusive"] = None
        done = finalize(capture["handle"], [end])
        assert done["ok"] and done["result"]["status"] == ending
    manifest = json.loads((path / "manifest.json").read_text(encoding="utf-8"))
    assert manifest["status"] == ending
    assert manifest["finalized"] is (ending != "interrupted")
    checked = inspect(path)
    assert checked["verification"] == "ok" and checked["acquisition_metadata"] == expected


@pytest.mark.parametrize("metadata", [
    {},
    {f"k{i}": "" for i in range(64)},
    {"é" * 64: "中" * 682 + "ab"},
    {f"k{i}": "a" * (2048 if i < 7 else 2032) for i in range(8)},
])
def test_acquisition_metadata_accepts_limits(tmp_path, metadata):
    d = descriptor("int16")
    response = request("capture_open", {"out_dir": str(tmp_path), "streams": [d],
        "host_clock": {"source": "monotonic", "boot_id": "host", "monotonic_base_ns": "0"},
        "acquisition_metadata": metadata})
    assert response["ok"], response
    assert inspect(response["result"]["path"])["acquisition_metadata"] == metadata


@pytest.mark.parametrize("metadata", [
    None, [], True, 3, "no", {"k": True}, {"k": 1}, {"k": 1.5}, {"k": None},
    {"k": {}}, {"k": []}, {1: "v"}, {"\ud800": "v"}, {"k": "\ud800"},
    {f"k{i}": "v" for i in range(65)}, {"é" * 65: "v"}, {"k": "中" * 683},
    {f"k{i}": "a" * (2048 if i < 7 else 2033) for i in range(8)},
    {"password": "secret"}, {"WiFi_Password": "secret"}, {"TOKEN": "secret"},
    {"access_token": "secret"}, {"api_key": "secret"}, {"authorization": "secret"},
])
def test_acquisition_metadata_invalid_open_creates_no_capture(tmp_path, metadata):
    response = request("capture_open", {"out_dir": str(tmp_path), "streams": [descriptor("int16")],
        "host_clock": {"source": "monotonic", "boot_id": "host", "monotonic_base_ns": "0"},
        "acquisition_metadata": metadata})
    assert response["error"]["code"] == "protocol.invalid_params"
    assert not list(tmp_path.iterdir())


@pytest.mark.parametrize("bad", [None, True, {"rate": 250}, {"rate": ["250"]},
                                  {"token": "secret"}, {"é" * 65: "v"}, {"rate": "中" * 683},
                                  {f"k{i}": "v" for i in range(65)},
                                  {f"k{i}": "a" * (2048 if i < 7 else 2033) for i in range(8)},
                                  {"rate": "\ud800"}])
def test_inspect_rejects_invalid_manifest_acquisition_metadata(tmp_path, bad):
    d = descriptor("int16")
    capture = opened(tmp_path, [d])
    assert append(capture["handle"], d, [[1], [2]])["ok"]
    assert finalize(capture["handle"], [summary(d, 1)])["ok"]
    path = Path(capture["path"])
    manifest_path = path / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    manifest["acquisition_metadata"] = bad
    manifest_path.write_text(json.dumps(manifest))
    before = manifest_path.read_bytes()
    checked = inspect(path)
    assert checked["verification"] == "issues"
    assert any(issue["code"] == "acquisition_metadata_invalid" for issue in checked["issues"])
    assert "acquisition_metadata" not in checked and checked["streams"][0]["verified_samples"] == "1"
    assert manifest_path.read_bytes() == before


def test_multiple_stream_gaps_events_and_partial_finalize(tmp_path):
    d1, d2 = descriptor("int24"), descriptor("float64", stream_id=9)
    opened_capture = opened(tmp_path, [d1, d2])
    handle, path = opened_capture["handle"], Path(opened_capture["path"])
    assert append(handle, d1, [[-1, 2], [3, 4]], first=2)["ok"]
    assert append(handle, d1, [[5, 6], [7, 8]], first=5)["ok"]
    assert append(handle, d2, [[0.125], [0.25]])["ok"]
    gap = {"event_id": str(uuid4()), "kind": "data.gap", "sample_index": None, "count": None,
           "params": {"reason": "disconnect", "detection": "adapter"}}
    assert request("capture_event", {"handle": handle, "record_type": "event", "stream": ident(d1), "record": gap})["ok"]
    mapping = {"clock_id": 3, "mapping_revision": 1, "valid_from_ticks": "123", "valid_to_ticks": None,
               "host_monotonic_ns": "9007199254740993", "offset_ns": "-100", "uncertainty_ns": None}
    assert request("capture_event", {"handle": handle, "record_type": "clock_mapping", "stream": ident(d1), "record": mapping})["ok"]
    wrong = finalize(handle, [summary(d1, 6), summary(d2, 1)])
    assert wrong["error"]["code"] == "protocol.invalid_params"
    done = finalize(handle, [summary(d1, 7, 9), summary(d2, 1)])
    assert done["ok"] and done["result"]["status"] == "partial"
    manifest = json.loads((path / "manifest.json").read_text())
    assert manifest["finalized"] is True
    assert manifest["streams"][0]["summary"]["last_accepted_sample_exclusive"] == "7"
    assert manifest["streams"][0]["summary"]["terminal_sample_exclusive"] == "9"
    with h5py.File(path / "signals.h5", "r") as h5:
        group = h5[f'streams/{d1["session_id"]}/1']
        assert group["samples"].shape == (4, 2)  # no fake rows for gaps
        assert [(int(x["start"]), int(x["count"])) for x in group["gaps"][:]] == [(0, 2), (4, 1), (0, 0), (7, 2)]
        assert list(group["blocks"][:]["sample_index"]) == [2, 5]
    assert len((path / "events.jsonl").read_text().splitlines()) == 4
    assert json.loads((path / "clock-mappings.jsonl").read_text())["offset_ns"] == "-100"
    checked = inspect(path)
    assert checked["verification"] == "ok" and checked["status"] == "partial"
    assert checked["recovery_advice"]


@pytest.mark.parametrize("reason", ["rt_bci.end_boundary_unknown", "rt_bci.unconfirmed_tail_groups=1"])
def test_terminal_unknown_gap_keeps_source_reason_and_sample_count(tmp_path, reason):
    d = descriptor("int16")
    capture = opened(tmp_path, [d])
    assert append(capture["handle"], d, [[1, 2], [3, 4]])["ok"]
    event = {"event_id": str(uuid4()), "kind": "data.gap", "sample_index": None, "count": None,
             "params": {"reason": reason, "detection": "adapter"}}
    assert request("capture_event", {"handle": capture["handle"], "record_type": "event",
                                    "stream": ident(d), "record": event})["ok"]
    end = summary(d, 2, unknown_gap=True)
    end["terminal_sample_exclusive"] = None
    done = finalize(capture["handle"], [end])
    assert done["ok"] and done["result"]["finalized"] and done["result"]["status"] == "partial"
    path = Path(capture["path"])
    events = [json.loads(line) for line in (path / "events.jsonl").read_text(encoding="utf-8").splitlines()]
    assert [entry["params"]["reason"] for entry in events] == [reason, "unknown_end_or_gap"]
    assert events[0]["stream"] == ident(d) and events[0]["params"]["detection"] == "adapter"
    assert all(entry["sample_index"] is None and entry["count"] is None for entry in events)
    with h5py.File(path / "signals.h5", "r") as h5:
        group = h5[f'streams/{d["session_id"]}/1']
        assert group["samples"][:].tolist() == [[1, 3], [2, 4]]
        assert len(group["gaps"]) == 2 and not group["gaps"][:]["count_known"].any()
    checked = inspect(path)
    assert checked["verification"] == "ok" and checked["streams"][0]["verified_samples"] == "2"
    assert checked["streams"][0]["gap_records"] == "2"  # records, not missing samples


def test_terminal_gap_write_failure_does_not_finalize(tmp_path, monkeypatch):
    from boundfree_sidecar.capture_io import session as module
    d = descriptor("int16")
    capture = opened(tmp_path, [d])
    assert append(capture["handle"], d, [[1], [2]])["ok"]
    def disk_full(*args):
        raise OSError(errno.ENOSPC, "No space left on device")
    monkeypatch.setattr(module, "line", disk_full)
    event = {"event_id": str(uuid4()), "kind": "data.gap", "sample_index": None, "count": None,
             "params": {"reason": "rt_bci.end_boundary_unknown", "detection": "adapter"}}
    response = request("capture_event", {"handle": capture["handle"], "record_type": "event",
                                         "stream": ident(d), "record": event})
    assert response["error"]["code"] == "capture.disk_full"
    assert finalize(capture["handle"], [summary(d, 1)])["error"]["code"] == "capture.disk_full"
    manifest = json.loads((Path(capture["path"]) / "manifest.json").read_text())
    assert manifest["status"] == "failed" and not manifest["finalized"]


def test_late_record_start_preserves_sample_index_and_can_be_complete(tmp_path):
    d = descriptor("int16")
    d["record_start_sample_index"] = "1000"
    capture = opened(tmp_path, [d])
    assert append(capture["handle"], d, [[-1, 2], [3, 4]], first=1000)["ok"]
    done = finalize(capture["handle"], [summary(d, 1002)])
    assert done["ok"] and done["result"]["status"] == "complete"
    manifest = json.loads((Path(capture["path"]) / "manifest.json").read_text())
    assert manifest["streams"][0]["descriptor"]["record_start_sample_index"] == "1000"
    checked = inspect(capture["path"])
    assert checked["verification"] == "ok"
    assert checked["streams"][0]["last_sample_exclusive"] == "1002"
    assert checked["streams"][0]["gap_records"] == "0"
    with h5py.File(Path(capture["path"]) / "signals.h5", "r") as h5:
        blocks = h5[f'streams/{d["session_id"]}/1/blocks']
        assert int(blocks[0]["sample_index"]) == 1000


def test_late_record_start_reports_actual_initial_loss(tmp_path):
    d = descriptor("int16")
    d["record_start_sample_index"] = "1000"
    capture = opened(tmp_path, [d])
    assert append(capture["handle"], d, [[1], [2]], first=1002)["ok"]
    done = finalize(capture["handle"], [summary(d, 1003)])
    assert done["ok"] and done["result"]["status"] == "partial"
    with h5py.File(Path(capture["path"]) / "signals.h5", "r") as h5:
        gap = h5[f'streams/{d["session_id"]}/1/gaps'][0]
        assert (int(gap["start"]), int(gap["count"])) == (1000, 2)
    assert inspect(capture["path"])["streams"][0]["last_sample_exclusive"] == "1003"


def test_late_record_start_zero_blocks_and_start_validation(tmp_path):
    d = descriptor("int16")
    d["record_start_sample_index"] = "1000"
    capture = opened(tmp_path, [d])
    assert not finalize(capture["handle"], [summary(d, 0)])["ok"]
    done = finalize(capture["handle"], [summary(d, 1000)])
    assert done["ok"] and done["result"]["status"] == "complete"
    assert inspect(capture["path"])["streams"][0]["last_sample_exclusive"] == "1000"
    d["record_start_sample_index"] = 1000
    response = request("capture_open", {"out_dir": str(tmp_path), "streams": [d],
        "host_clock": {"source": "monotonic", "boot_id": "host", "monotonic_base_ns": "0"}})
    assert response["error"]["code"] == "protocol.invalid_params"


def test_identity_shape_overlap_duplicate_and_u64_validation(tmp_path):
    d = descriptor("int32")
    handle = opened(tmp_path, [d])["handle"]
    data = [[1, 2], [3, 4]]
    wrong = ident(d)
    wrong["generation"] = "9007199254740994"
    assert append(handle, d, data, stream=wrong)["error"]["code"] == "protocol.invalid_params"
    assert append(handle, d, data, shape=[2, 3])["error"]["code"] == "protocol.invalid_params"
    assert append(handle, d, data, first_sample_index=0)["error"]["code"] == "protocol.invalid_params"
    assert append(handle, d, data)["ok"]
    duplicate = append(handle, d, data)
    assert duplicate["ok"] and duplicate["result"]["duplicate"] and not duplicate["result"]["accepted"]
    assert append(handle, d, [[2, 3], [4, 5]], first=1)["error"]["code"] == "protocol.invalid_params"
    assert append(handle, d, [[1, 9], [3, 4]], first=0)["error"]["code"] == "protocol.invalid_params"
    assert append(handle, d, [[1], [2]], first=(1 << 64) - 1)["error"]["code"] == "protocol.invalid_params"
    done = finalize(handle, [summary(d, 2)])
    assert done["result"]["streams"][0]["duplicate_blocks"] == "1"


def test_nonfinite_float_rejected_and_empty_unknown_end_is_partial(tmp_path):
    d = descriptor("float64")
    result = opened(tmp_path, [d])
    response = append(result["handle"], d, [[float("nan")], [1.0]])
    assert response["error"]["code"] == "protocol.invalid_params"
    end = summary(d, 0)
    end["terminal_sample_exclusive"] = None
    end["complete"] = False
    done = finalize(result["handle"], [end])
    assert done["ok"] and done["result"]["status"] == "partial"
    assert done["result"]["streams"][0]["durable_samples"] is None


def test_nonfinite_block_records_known_gap_even_if_caller_reports_complete(tmp_path):
    d = descriptor("float32")
    capture = opened(tmp_path, [d])
    assert not append(capture["handle"], d, [[float("inf")], [1.0]])["ok"]
    done = finalize(capture["handle"], [summary(d, 0)])
    assert done["ok"] and done["result"]["status"] == "partial"
    with h5py.File(Path(capture["path"]) / "signals.h5", "r") as h5:
        gap = h5[f'streams/{d["session_id"]}/1/gaps'][0]
        assert (int(gap["start"]), int(gap["count"])) == (0, 1)


def test_background_flush_reports_only_flush_not_durability(tmp_path, monkeypatch):
    from boundfree_sidecar.capture_io import session
    monkeypatch.setattr(session, "FLUSH_INTERVAL_SECONDS", 0.02)
    d = descriptor("int16")
    handle = opened(tmp_path, [d])["handle"]
    assert append(handle, d, [[1], [2]])["ok"]
    state = capture_io._registry.sessions[handle].streams[(d["session_id"], d["stream_id"])]
    deadline = time.monotonic() + 2
    while state["flushed"] != 1 and time.monotonic() < deadline:
        time.sleep(0.01)
    assert state["flushed"] == 1
    response = append(handle, d, [[3], [4]], first=1)
    assert int(response["result"]["flushed_samples"]) >= 1
    assert response["result"]["durable_samples"] is None
    assert finalize(handle, [summary(d, 2)])["ok"]


def test_clock_id_zero_and_known_tail_with_unknown_gap(tmp_path):
    d = descriptor("int16")
    d["clock"]["clock_id"] = 0
    capture = opened(tmp_path, [d])
    assert append(capture["handle"], d, [[1], [2]], clock_id=0)["ok"]
    end = summary(d, 1, 4, unknown_gap=True)
    done = finalize(capture["handle"], [end])
    assert done["ok"] and done["result"]["status"] == "partial"
    with h5py.File(Path(capture["path"]) / "signals.h5", "r") as h5:
        gaps = h5[f'streams/{d["session_id"]}/1/gaps'][:]
        assert len(gaps) == 2
        assert (int(gaps[0]["start_known"]), int(gaps[0]["count_known"])) == (0, 0)
        assert (int(gaps[1]["start"]), int(gaps[1]["count"])) == (1, 3)


def test_background_write_failure_is_reported_on_finalize(tmp_path, monkeypatch):
    from boundfree_sidecar.capture_io import session as module
    monkeypatch.setattr(module, "FLUSH_INTERVAL_SECONDS", 0.02)
    d = descriptor("int16")
    capture = opened(tmp_path, [d])
    session = capture_io._registry.sessions[capture["handle"]]
    def disk_full():
        raise OSError(errno.ENOSPC, "No space left on device")
    monkeypatch.setattr(session, "_flush", disk_full)
    assert append(capture["handle"], d, [[1], [2]])["ok"]
    assert session.stop_timer.wait(2)
    response = finalize(capture["handle"], [summary(d, 1)])
    assert response["error"]["code"] == "capture.disk_full"
    assert capture["handle"] not in capture_io._registry.sessions
    assert not json.loads((Path(capture["path"]) / "manifest.json").read_text())["finalized"]


def test_disk_full_closes_failed_session_and_does_not_publish_success(tmp_path, monkeypatch):
    d = descriptor("int16")
    opened_capture = opened(tmp_path, [d])
    handle, path = opened_capture["handle"], Path(opened_capture["path"])
    assert append(handle, d, [[1], [2]])["ok"]
    session = capture_io._registry.sessions[handle]
    def disk_full():
        raise OSError(errno.ENOSPC, "No space left on device")
    monkeypatch.setattr(session, "_flush", disk_full)
    response = finalize(handle, [summary(d, 1)])
    assert response["error"]["code"] == "capture.disk_full"
    assert handle not in capture_io._registry.sessions
    manifest = json.loads((path / "manifest.json").read_text())
    assert not manifest["finalized"] and manifest["status"] == "failed"
    assert manifest["failure"]["code"] == "capture.disk_full"


def test_final_manifest_publish_failure_cannot_claim_finalized(tmp_path, monkeypatch):
    from boundfree_sidecar.capture_io import session as module
    d = descriptor("int16")
    opened_capture = opened(tmp_path, [d])
    handle, path = opened_capture["handle"], Path(opened_capture["path"])
    assert append(handle, d, [[1], [2]])["ok"]
    original = module.atomic_manifest
    def fail_success(path, manifest):
        if manifest["finalized"]:
            raise OSError(errno.EIO, "publish failed")
        return original(path, manifest)
    monkeypatch.setattr(module, "atomic_manifest", fail_success)
    assert finalize(handle, [summary(d, 1)])["error"]["code"] == "capture.writer_failed"
    manifest = json.loads((path / "manifest.json").read_text())
    assert manifest["status"] == "failed" and not manifest["finalized"]


def test_inspect_reports_corruption_without_modifying_files(tmp_path):
    d = descriptor("int16")
    opened_capture = opened(tmp_path, [d])
    handle, path = opened_capture["handle"], Path(opened_capture["path"])
    assert append(handle, d, [[1, 2], [3, 4]])["ok"]
    assert finalize(handle, [summary(d, 2)])["ok"]
    with h5py.File(path / "signals.h5", "r+") as h5:
        h5[f'streams/{d["session_id"]}/1/samples'][0, 0] = 99
    before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in path.iterdir() if p.is_file()}
    checked = inspect(path)
    assert checked["verification"] == "issues"
    assert any(x["code"] == "block_checksum_mismatch" for x in checked["issues"])
    after = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in path.iterdir() if p.is_file()}
    assert before == after


def test_stdio_envelope_binary_pair_and_cleanup_on_eof(tmp_path):
    from boundfree_sidecar.framing import FRAME_BINARY, read_json_frame, write_frame, write_json_frame
    root = Path(__file__).resolve().parents[2]
    env = {**os.environ, "PYTHONPATH": str(root / "sidecar"), "PYTHONUTF8": "1"}
    process = subprocess.Popen([sys.executable, "-u", "-m", "boundfree_sidecar", "--stdio"], env=env,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        def call(method, params, payload=None):
            write_json_frame(process.stdin, {"id": 5, "method": method, "params": params})
            if payload is not None:
                write_frame(process.stdin, FRAME_BINARY, payload)
            return read_json_frame(process.stdout)
        hello = call("hello", {"requested_capabilities": ["capture.v1"]})
        assert hello["result"]["capabilities"] == ["capture.v1"]
        hello_metadata = call("hello", {"requested_capabilities": ["capture.v1", "capture.metadata.v1"]})
        assert hello_metadata["result"]["capabilities"] == ["capture.v1", "capture.metadata.v1"]
        d = descriptor("int24")
        opened_capture = call("capture_open", {"out_dir": str(tmp_path), "streams": [d],
            "host_clock": {"source": "monotonic", "boot_id": "host", "monotonic_base_ns": "0"}})["result"]
        metadata = {"rate_origin": "requested", "gain_readback": "unknown", "说明": "未读回"}
        metadata_capture = call("capture_open", {"out_dir": str(tmp_path), "streams": [d],
            "host_clock": {"source": "monotonic", "boot_id": "host", "monotonic_base_ns": "0"},
            "acquisition_metadata": metadata})["result"]
        p = {"handle": opened_capture["handle"], "stream": ident(d), "shape": [2, 1], "sample_count": 1,
             "dtype": "int24", "first_sample_index": "0", "first_timestamp_ticks": None,
             "host_received_monotonic_ns": "1", "clock_id": 3, "time_flags": 0, "quality_flags": 0}
        assert call("capture_append", p, encoded([[-8388608], [8388607]], "int24"))["ok"]
        assert call("hello", {})["ok"]
        process.stdin.close()
        assert process.wait(timeout=10) == 0
        manifest = json.loads((Path(opened_capture["path"]) / "manifest.json").read_text())
        assert manifest["status"] == "interrupted" and not manifest["finalized"]
        assert "acquisition_metadata" not in manifest
        assert inspect(opened_capture["path"])["streams"][0]["verified_samples"] == "1"
        metadata_manifest = json.loads((Path(metadata_capture["path"]) / "manifest.json").read_text(encoding="utf-8"))
        assert metadata_manifest["status"] == "interrupted" and not metadata_manifest["finalized"]
        assert metadata_manifest["acquisition_metadata"] == metadata
        assert inspect(metadata_capture["path"])["acquisition_metadata"] == metadata
    finally:
        if process.stdin and not process.stdin.closed:
            process.stdin.close()
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
