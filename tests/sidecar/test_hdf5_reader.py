from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
h5py = pytest.importorskip("h5py")


def _write_hdf5_fixture(path: Path) -> None:
    with h5py.File(path, "w") as f:
        ds = f.create_dataset(
            "eeg",
            data=[
                [0.0, 1.0, 2.0, 3.0, 4.0],
                [10.0, 11.0, 12.0, 13.0, 14.0],
            ],
        )
        ds.attrs["fs"] = 100.0
        ds.attrs["ch_names"] = ["h1", "h2"]
        ds.attrs["unit"] = "uV"


def test_hdf5_reader_open_returns_default_schema_metadata(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    sample = tmp_path / "sample.h5"
    _write_hdf5_fixture(sample)

    response = handle_request({"id": "h5-open", "method": "open", "params": {"path": str(sample)}})

    assert response["id"] == "h5-open"
    assert response["ok"] is True
    assert response["result"]["format"] == "HDF5"
    assert response["result"]["fs"] == 100.0
    assert response["result"]["n_channels"] == 2
    assert response["result"]["n_samples"] == 5
    assert response["result"]["duration"] == 0.05
    assert response["result"]["ch_names"] == ["h1", "h2"]
    assert response["result"]["unit"] == "uV"


def test_hdf5_reader_get_slice_returns_requested_window(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    sample = tmp_path / "sample.h5"
    _write_hdf5_fixture(sample)
    opened = handle_request({"id": "h5-open-2", "method": "open", "params": {"path": str(sample)}})
    handle = opened["result"]["handle"]

    response = handle_request(
        {"id": "h5-slice", "method": "get_slice", "params": {"handle": handle, "t0": 0.01, "t1": 0.04}}
    )

    assert response["id"] == "h5-slice"
    assert response["ok"] is True
    assert response["result"]["shape"] == [2, 3]
    assert response["result"]["data"] == [[1.0, 2.0, 3.0], [11.0, 12.0, 13.0]]
