from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_csv_reader_open_includes_marker_sidecar_events(tmp_path):
    """Opening a main CSV automatically loads events from its _{sidecar}_markers.csv."""
    from boundfree_sidecar.protocol import handle_request

    eeg = tmp_path / "recording.csv"
    eeg.write_text("time_s,ch1,ch2\n0.00,0.0,10.0\n0.01,1.0,11.0\n", encoding="utf-8")
    markers = tmp_path / "recording_markers.csv"
    markers.write_text("time_s,label\n0.005,stim_a\n", encoding="utf-8")

    response = handle_request({"id": "open-markers", "method": "open", "params": {"path": str(eeg)}})

    assert response["id"] == "open-markers"
    assert response["ok"] is True
    assert response["result"]["events"] == [
        {"time": 0.005, "label": "stim_a"},
    ]


def test_reader_open_exception_returns_structured_error(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    bad_csv = tmp_path / "bad.csv"
    bad_csv.write_text("time_s,ch1\n", encoding="utf-8")

    response = handle_request({"id": "open-bad", "method": "open", "params": {"path": str(bad_csv)}})

    assert response["id"] == "open-bad"
    assert response["ok"] is False
    assert response["error"]["code"] == "reader.open_failed"
    assert response["error"]["params"]["path"] == str(bad_csv)
    assert response["error"]["params"]["reason"]


def test_open_marker_sidecar_directly_returns_marker_sidecar_error(tmp_path):
    """Directly opening a _markers.csv file rejects with reader.marker_sidecar and the raw path."""
    from boundfree_sidecar.protocol import handle_request

    sidecar = tmp_path / "recording_markers.csv"
    sidecar.write_text("time_s,label\n0.02,stim_a\n0.07,stim_b\n", encoding="utf-8")

    response = handle_request({"id": "open-sidecar", "method": "open", "params": {"path": str(sidecar)}})

    assert response["id"] == "open-sidecar"
    assert response["ok"] is False
    assert response["error"]["code"] == "reader.marker_sidecar"
    assert response["error"]["params"]["path"] == str(sidecar)
