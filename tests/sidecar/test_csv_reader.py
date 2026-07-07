from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"


def test_csv_reader_metadata_does_not_cache_all_rows():
    from boundfree_sidecar.readers.csv_reader import CsvReader

    reader = CsvReader(SAMPLE)

    meta = reader.metadata()

    assert meta.n_samples == 10
    assert reader._rows is None


def test_csv_reader_open_returns_metadata_only():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "csv-open", "method": "open", "params": {"path": str(SAMPLE)}})

    assert response["id"] == "csv-open"
    assert response["ok"] is True
    result = response["result"]
    assert result["handle"].startswith("file-")
    assert result["fs"] == 100.0
    assert result["n_channels"] == 2
    assert result["n_samples"] == 10
    assert result["duration"] == 0.1
    assert result["ch_names"] == ["ch1", "ch2"]
    assert result["unit"] == "uV"
    assert result["format"] == "CSV"
    assert result["events"] == []


def test_csv_reader_get_slice_returns_visible_window_data():
    from boundfree_sidecar.protocol import handle_request

    opened = handle_request({"id": "csv-open-2", "method": "open", "params": {"path": str(SAMPLE)}})
    handle = opened["result"]["handle"]

    response = handle_request({"id": "csv-slice", "method": "get_slice", "params": {"handle": handle, "t0": 0.02, "t1": 0.05}})

    assert response["id"] == "csv-slice"
    assert response["ok"] is True
    assert response["result"] == {
        "handle": handle,
        "t0": 0.02,
        "t1": 0.05,
        "fs": 100.0,
        "shape": [2, 3],
        "unit": "uV",
        "data": [[2.0, 3.0, 4.0], [12.0, 13.0, 14.0]],
    }
