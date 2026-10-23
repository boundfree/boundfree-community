import io
import json
from pathlib import Path

import numpy as np
import pytest

from boundfree_sidecar import analysis_io
from boundfree_sidecar.analysis_io.sources import AnalysisError
from boundfree_sidecar.framing import FRAME_BINARY, read_frame, write_frame, write_json_frame
from boundfree_sidecar.protocol import handle_request
from boundfree_sidecar.stdio import run_stdio

ROOT = Path(__file__).resolve().parents[2]
INTAN_FIXTURES = ROOT / "tests" / "fixtures" / "generated" / "offline-eeg"


def call(method, **params):
    return handle_request({"id": "analysis-test", "method": method, "params": params})


@pytest.fixture(autouse=True)
def clean_registry():
    yield
    analysis_io.cleanup()


@pytest.fixture
def hdf_source(tmp_path):
    h5py = pytest.importorskip("h5py")
    path = tmp_path / "source.h5"
    values = np.arange(4000, dtype=np.float64).reshape(2, 2000) * 1.2345678901234567
    with h5py.File(path, "w") as output:
        ds = output.create_dataset("eeg", data=values)
        ds.attrs.update(fs=500., unit="uV", ch_names=["Cz", "EOG"])
    opened = call("analysis_source_open", path=str(path))
    assert opened["ok"], opened
    return path, values, opened["result"]


def test_hello_advertises_incremental_capabilities():
    result = call("hello")["result"]
    assert result["ipc_protocol_version"] == 1
    assert result["analysis_capabilities"] == ["source-f64-v1", "bundle-f64-v1"]


def test_hdf_integer_slice_preserves_order_and_double(hdf_source):
    _, expected, meta = hdf_source
    response = call("analysis_source_read", handle=meta["handle"], first_sample=17,
                    n_samples=53, channel_ids=["1", "0"])
    assert response["ok"], response
    result = response["result"]
    np.testing.assert_array_equal(result["data"], expected[[1, 0], 17:70])
    assert result["dtype"] == "float64-le" and result["payload_bytes"] == 2 * 53 * 8
    assert call("analysis_source_close", handle=meta["handle"])["ok"]
    assert call("analysis_source_read", handle=meta["handle"])["error"]["code"] == "analysis.handle_not_found"


@pytest.mark.parametrize("change", ["append", "same-size-preserved-mtime"])
def test_source_mutation_rejected(hdf_source, change):
    path, _, meta = hdf_source
    original = path.stat()
    if change == "append":
        with path.open("ab") as stream:
            stream.write(b"changed")
    else:
        with path.open("r+b") as stream:
            stream.seek(-1, 2)
            stream.write(b"Z")
        import os
        os.utime(path, ns=(original.st_atime_ns, original.st_mtime_ns))
    response = call("analysis_source_read", handle=meta["handle"], first_sample=0, n_samples=4, channel_ids=["0"])
    assert response["error"]["code"] == "analysis.source_changed"


@pytest.mark.parametrize("params", [{"first_sample": -1, "n_samples": 10, "channel_ids": ["0"]},
                                   {"first_sample": 0, "n_samples": 10, "channel_ids": ["0", "0"]},
                                   {"first_sample": 0, "n_samples": 10, "channel_ids": ["000"]},
                                   {"first_sample": True, "n_samples": 10, "channel_ids": ["0"]},
                                   {"first_sample": 1999, "n_samples": 2, "channel_ids": ["0"]}])
def test_integer_ranges_and_ids_rejected(hdf_source, params):
    response = call("analysis_source_read", handle=hdf_source[2]["handle"], **params)
    assert response["error"]["code"] == "analysis.invalid_request"


def test_resource_limit_checked_before_read(hdf_source, monkeypatch):
    import importlib
    sources_module = importlib.import_module("boundfree_sidecar.analysis_io.sources")
    monkeypatch.setattr(sources_module, "MAX_BYTES", 15)
    snapshot = analysis_io.source_registry().sources[hdf_source[2]["handle"]]
    monkeypatch.setattr(snapshot.reader, "read", lambda *args: pytest.fail("reader must not run"))
    response = call("analysis_source_read", handle=hdf_source[2]["handle"], first_sample=0,
                    n_samples=2, channel_ids=["0"])
    assert response["error"]["code"] == "analysis.resource_limit"


def test_unknown_unit_and_unverified_format_rejected(tmp_path):
    h5py = pytest.importorskip("h5py")
    path = tmp_path / "unknown.h5"
    with h5py.File(path, "w") as stream:
        ds = stream.create_dataset("eeg", data=np.zeros((2, 50)))
        ds.attrs.update(fs=500, ch_names=["a", "b"])
    assert call("analysis_source_open", path=str(path))["error"]["code"] == "analysis.unknown_unit"
    path = tmp_path / "unsupported.xyz"
    path.write_bytes(b"anything")
    assert call("analysis_source_open", path=str(path))["error"]["code"] == "analysis.unsupported_source"


def bundle_params(hdf_source, tmp_path):
    path, values, meta = hdf_source
    values = values[:, :137]
    data = np.zeros((7, *values.shape), dtype="<f8")
    mean = np.array([3.25, -27.125])
    data[0] = values
    data[1] = values
    data[5] = values - mean[:, None]
    params = {"path": str(tmp_path / "result"), "fs": 500., "t0": 1.25,
              "ch_names": ["Cz", "EOG"], "shape": list(data.shape), "dtype": "float64-le", "unit": "uV",
              "manifest": {"source": {"path": str(path), "fingerprint": meta["fingerprint"]},
                           "mean": mean.tolist(), "labels": ["unresolved", "unresolved"],
                           "software_version": "test", "manual_confirmation": True}}
    return params, data


def write_bundle(params, data):
    return handle_request({"id": "bundle", "method": "analysis_bundle_write", "params": params}, data.tobytes())


def test_bundle_stage_publish_float64_hashes_and_no_overwrite(hdf_source, tmp_path):
    params, data = bundle_params(hdf_source, tmp_path)
    response = write_bundle({**params, "stage_only": True}, data)
    assert response["ok"], response
    staged = response["result"]
    assert not Path(params["path"]).exists()
    assert Path(staged["temporary_path"]).is_dir()
    assert staged["readback_error_uV"] <= 1e-6
    published = call("analysis_bundle_publish", token=staged["token"])
    assert published["ok"], published
    directory = Path(params["path"])
    assert len(list(directory.iterdir())) == 8
    manifest = json.loads((directory / "analysis.json").read_text())
    assert manifest["schema"] == "boundfree.community.offline-eeg.v1"
    assert manifest["group_identity"] == "analysis = mean + eeg + eog + emg + unresolved + residual"
    assert len(manifest["files"]) == 7
    assert len(published["result"]["files"]) == 8
    loaded = np.loadtxt(directory / "analysis_uV.csv", delimiter=",", skiprows=1)
    np.testing.assert_array_equal(loaded[:, 1:].T, data[1])
    assert write_bundle(params, data)["error"]["code"] == "analysis.destination_exists"


def test_bad_reconstruction_never_leaves_result(hdf_source, tmp_path):
    params, data = bundle_params(hdf_source, tmp_path)
    data[5, 0, 0] += 0.001
    response = write_bundle(params, data)
    assert response["error"]["code"] == "analysis.reconstruction_failed"
    assert not Path(params["path"]).exists()
    assert not list(tmp_path.glob(".boundfree-analysis-*"))


def test_discard_and_eof_cleanup(hdf_source, tmp_path):
    params, data = bundle_params(hdf_source, tmp_path)
    first = write_bundle({**params, "stage_only": True}, data)["result"]
    assert call("analysis_bundle_discard", token=first["token"])["ok"]
    assert not Path(first["temporary_path"]).exists()
    second = write_bundle({**params, "stage_only": True}, data)["result"]
    assert run_stdio(io.BytesIO(), io.BytesIO()) == 0
    assert not Path(second["temporary_path"]).exists()
    assert not Path(params["path"]).exists()


def test_source_change_prevents_publish(hdf_source, tmp_path):
    params, data = bundle_params(hdf_source, tmp_path)
    staged = write_bundle({**params, "stage_only": True}, data)["result"]
    with hdf_source[0].open("ab") as stream:
        stream.write(b"changed")
    response = call("analysis_bundle_publish", token=staged["token"])
    assert response["error"]["code"] == "analysis.source_changed"
    assert not Path(params["path"]).exists()


def test_existing_empty_destination_not_overwritten_on_publish(hdf_source, tmp_path):
    params, data = bundle_params(hdf_source, tmp_path)
    staged = write_bundle({**params, "stage_only": True}, data)["result"]
    Path(params["path"]).mkdir()
    response = call("analysis_bundle_publish", token=staged["token"])
    assert response["error"]["code"] == "analysis.destination_exists"
    assert list(Path(params["path"]).iterdir()) == []


def test_stdio_double_read_preserves_get_slice_contract(hdf_source):
    request = {"id": 7, "method": "analysis_source_read", "params": {
        "handle": hdf_source[2]["handle"], "first_sample": 11, "n_samples": 19, "channel_ids": ["1", "0"]}}
    incoming, outgoing = io.BytesIO(), io.BytesIO()
    write_json_frame(incoming, request)
    incoming.seek(0)
    assert run_stdio(incoming, outgoing) == 0
    outgoing.seek(0)
    control, binary = read_frame(outgoing), read_frame(outgoing)
    response = json.loads(control.payload)
    assert response["result"]["dtype"] == "float64-le"
    assert binary.frame_type == FRAME_BINARY
    np.testing.assert_array_equal(np.frombuffer(binary.payload, dtype="<f8").reshape(2, 19), hdf_source[1][[1, 0], 11:30])


@pytest.mark.parametrize("suffix", ["rhd", "rhs"])
def test_real_synthetic_intan_chunk_matches_neo_and_blocks_timestamp_copy(suffix, monkeypatch):
    pytest.importorskip("neo")
    from neo.rawio import IntanRawIO
    sample = INTAN_FIXTURES / ("synthetic." + suffix)
    if not sample.exists():
        pytest.fail("repository controlled Intan fixture missing")
    # This verifies the path doesn't accidentally fall back to Neo whole-file timestamp flatten.
    monkeypatch.setattr(IntanRawIO, "get_intan_timestamps", lambda *a, **k: pytest.fail("full timestamp copy"))
    opened = call("analysis_source_open", path=str(sample))
    assert opened["ok"], opened
    result = opened["result"]
    json.dumps(result)  # Native metadata must contain no NumPy scalar types.
    assert result["fs"] == 2000 and result["n_channels"] == 6
    read = call("analysis_source_read", handle=result["handle"], first_sample=123,
                n_samples=279, channel_ids=["4", "0", "1"])
    assert read["ok"], read
    snapshot = analysis_io.source_registry().sources[result["handle"]]
    raw = snapshot.reader.raw
    expected = raw.rescale_signal_raw_to_float(raw.get_analogsignal_chunk(
        i_start=123, i_stop=402, stream_index=0, channel_indexes=[4, 0, 1]),
        dtype="float64", stream_index=0, channel_indexes=[4, 0, 1]).T
    np.testing.assert_array_equal(read["result"]["data"], expected)


@pytest.mark.parametrize("rollover", [False, True])
def test_intan_timestamp_gap_rejected_and_rollover_accepted(tmp_path, rollover):
    pytest.importorskip("neo")
    import shutil
    from neo.rawio import IntanRawIO
    sample = INTAN_FIXTURES / "synthetic.rhd"
    if not sample.exists():
        pytest.fail("repository controlled Intan fixture missing")
    copy = tmp_path / "changed.rhd"
    shutil.copyfile(sample, copy)
    raw = IntanRawIO(filename=str(copy))
    raw.parse_header()
    dtype, offset = raw._raw_data.dtype, raw._raw_data.offset
    writable = np.memmap(copy, mode="r+", dtype=dtype, offset=offset)
    ticks = writable["timestamp"]
    if rollover:
        ticks[:] = (np.arange(ticks.size, dtype=np.uint32).reshape(ticks.shape) + np.uint32(2**32 - 200)).view(np.int32)
    else:
        ticks[1, 3] += 2
    writable.flush()
    response = call("analysis_source_open", path=str(copy))
    if rollover:
        assert response["ok"], response
    else:
        assert response["error"]["code"] == "analysis.discontinuous_time"


def test_stdio_bundle_binary_request_stays_float64_and_cleans_staged_result(hdf_source, tmp_path):
    params, data = bundle_params(hdf_source, tmp_path)
    incoming, outgoing = io.BytesIO(), io.BytesIO()
    write_json_frame(incoming, {"id": 8, "method": "analysis_bundle_write", "params": {**params, "stage_only": True}})
    write_frame(incoming, FRAME_BINARY, data.tobytes())
    incoming.seek(0)
    assert run_stdio(incoming, outgoing) == 0
    outgoing.seek(0)
    response = json.loads(read_frame(outgoing).payload)
    assert response["ok"], response
    assert response["result"]["staged"] is True
    assert response["result"]["readback_error_uV"] <= 1e-6
    assert not Path(response["result"]["temporary_path"]).exists()  # EOF discards unconfirmed bundle
    assert not Path(params["path"]).exists()


@pytest.mark.parametrize("rail_count,accepted", [(1, True), (3, False)])
def test_intan_raw_rail_counts_use_adc_codes_and_threshold(tmp_path, rail_count, accepted):
    pytest.importorskip("neo")
    import shutil
    from neo.rawio import IntanRawIO
    sample = INTAN_FIXTURES / "synthetic.rhd"
    if not sample.exists():
        pytest.fail("repository controlled Intan fixture missing")
    copy = tmp_path / "rails.rhd"
    shutil.copyfile(sample, copy)
    raw = IntanRawIO(filename=str(copy))
    raw.parse_header()
    writable = np.memmap(copy, mode="r+", dtype=raw._raw_data.dtype, offset=raw._raw_data.offset)
    channels = raw.header["signal_channels"]
    channel = channels[channels["stream_id"] == "0"][0]
    field = writable[str(channel["id"])]
    field[0, :rail_count] = np.iinfo(field.dtype).min
    writable.flush()
    opened = call("analysis_source_open", path=str(copy))["result"]
    response = call("analysis_source_read", handle=opened["handle"], first_sample=0,
                    n_samples=2000, channel_ids=["0"])
    if accepted:
        assert response["ok"], response
        saturation = response["result"]["saturation"]["channels"][0]
        assert saturation["rail_samples"] == rail_count
        assert saturation["raw_rails"] == [0, 65535]
        assert saturation["rails_uV"][0] == -6389.76
        json.dumps(response["result"]["saturation"])
    else:
        assert response["error"]["code"] == "analysis.adc_saturation"
        assert response["error"]["params"] == {"channel_id": "0", "rail_samples": rail_count, "n_samples": 2000}


@pytest.mark.parametrize("phase", ["before-write", "during-write", "during-readback"])
def test_cooperative_cancel_removes_hidden_stage_and_never_publishes(hdf_source, tmp_path, monkeypatch, phase):
    import importlib
    module = importlib.import_module("boundfree_sidecar.analysis_io.bundle")
    params, data = bundle_params(hdf_source, tmp_path)
    # At least 257 samples to exercise a bounded in-group cancellation check.
    data = np.tile(data, (1, 1, 3))
    params["shape"] = list(data.shape)
    cancellation = tmp_path / "cancel-control"
    params["cancel_path"] = str(cancellation)
    if phase == "before-write":
        cancellation.touch()
    elif phase == "during-write":
        original = module.csv.writer
        def cancelling_writer(*args, **kwargs):
            wrapped = original(*args, **kwargs)
            class Writer:
                rows = 0
                def writerow(self, row):
                    wrapped.writerow(row)
                    self.rows += 1
                    if self.rows == 97:
                        cancellation.touch()
            return Writer()
        monkeypatch.setattr(module.csv, "writer", cancelling_writer)
    else:
        original = module.csv.reader
        def cancelling_reader(*args, **kwargs):
            for index, row in enumerate(original(*args, **kwargs)):
                if index == 97:
                    cancellation.touch()
                yield row
        monkeypatch.setattr(module.csv, "reader", cancelling_reader)
    response = write_bundle({**params, "stage_only": True}, data)
    assert response["error"]["code"] == "analysis.cancelled"
    assert not Path(params["path"]).exists()
    assert not list(tmp_path.glob(".boundfree-analysis-*"))
    assert not analysis_io.bundle_registry().stages


def test_cancel_after_staging_prevents_publish_and_discards_stage(hdf_source, tmp_path):
    params, data = bundle_params(hdf_source, tmp_path)
    cancellation = tmp_path / "cancel-control"
    response = write_bundle({**params, "stage_only": True, "cancel_path": str(cancellation)}, data)
    assert response["ok"], response
    stage = response["result"]
    cancellation.touch()
    response = call("analysis_bundle_publish", token=stage["token"])
    assert response["error"]["code"] == "analysis.cancelled"
    assert not Path(params["path"]).exists()
    assert not Path(stage["temporary_path"]).exists()
    assert not analysis_io.bundle_registry().stages
