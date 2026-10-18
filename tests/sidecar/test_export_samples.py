"""Native-prepared samples must survive encoding, validation and framed IPC."""
from io import BytesIO
from pathlib import Path

import numpy as np
import pytest

from boundfree_sidecar.framing import FRAME_BINARY, read_json_frame, write_frame, write_json_frame
from boundfree_sidecar.protocol import handle_request
from boundfree_sidecar.readers.registry import reader_for_path
from boundfree_sidecar.stdio import run_stdio


def request(path: Path):
    data = np.asarray([[1.0, -2.0, 3.0, -4.0]], dtype="<f4")
    return {
        "id": "processed", "method": "export_samples",
        "params": {"path": str(path), "fs": 100.0, "ch_names": ["a-b"],
                   "shape": [1, 4], "dtype": "float32-le", "unit": "uV",
                   "t0": 2.0, "t1": 2.04,
                   "markers": [{"time": 2.01, "label": "stim"}, {"time": 1.0, "label": "outside"}]},
    }, data


@pytest.mark.parametrize("suffix", ["csv", "h5", "edf"])
def test_prepared_samples_keep_output_channels_values_and_events(tmp_path, suffix):
    message, data = request(tmp_path / f"processed.{suffix}")
    result = handle_request(message, data.tobytes())
    assert result["ok"], result
    assert result["result"]["n_channels"] == 1
    assert result["result"]["n_samples"] == 4
    assert result["result"]["marker_count"] == 1
    reader = reader_for_path(Path(message["params"]["path"]))
    meta = reader.metadata()
    assert meta.ch_names == ["a-b"]
    assert meta.fs == pytest.approx(100.0)
    assert meta.events[0]["label"] == "stim"
    assert meta.events[0]["time"] == pytest.approx(0.01, abs=1e-6)
    np.testing.assert_allclose(reader.get_slice(0, 0.04).data, data, atol=0.1 if suffix == "edf" else 1e-6)


@pytest.mark.parametrize("field,value,code", [
    ("fs", 0, "protocol.invalid_params"),
    ("fs", True, "protocol.invalid_params"),
    ("ch_names", [], "protocol.invalid_params"),
    ("shape", [2, 4], "ipc.invalid_frame"),
    ("shape", [True, 4], "ipc.invalid_frame"),
    ("dtype", "float64", "ipc.invalid_frame"),
    ("unit", "V", "ipc.invalid_frame"),
    ("t1", 1.0, "export.invalid_range"),
])
def test_invalid_prepared_metadata_rejected_before_writing(tmp_path, field, value, code):
    message, data = request(tmp_path / "invalid.csv")
    message["params"][field] = value
    result = handle_request(message, data.tobytes())
    assert not result["ok"]
    assert result["error"]["code"] == code
    assert not Path(message["params"]["path"]).exists()


def test_payload_validation_and_next_request_remain_framed(tmp_path):
    message, data = request(tmp_path / "invalid.csv")
    source, output = BytesIO(), BytesIO()
    write_json_frame(source, message)
    write_frame(source, FRAME_BINARY, data.tobytes()[:-4])
    write_json_frame(source, {"id": "next", "method": "hello"})
    source.seek(0)
    assert run_stdio(source, output) == 0
    output.seek(0)
    assert read_json_frame(output)["error"]["code"] == "ipc.invalid_frame"
    assert read_json_frame(output)["id"] == "next"


def test_prepared_samples_travel_through_stdio(tmp_path):
    message, data = request(tmp_path / "framed.csv")
    source, output = BytesIO(), BytesIO()
    write_json_frame(source, message)
    write_frame(source, FRAME_BINARY, data.tobytes())
    source.seek(0)
    assert run_stdio(source, output) == 0
    output.seek(0)
    assert read_json_frame(output)["ok"]


def test_replacing_processed_export_does_not_keep_old_annotations(tmp_path):
    message, data = request(tmp_path / "reused.csv")
    assert handle_request(message, data.tobytes())["ok"]
    message["params"]["markers"] = []
    assert handle_request(message, data.tobytes())["ok"]
    assert reader_for_path(tmp_path / "reused.csv").metadata().events == []
