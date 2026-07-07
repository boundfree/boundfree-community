from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_csv_reader_open_includes_marker_sidecar_events():
    from boundfree_sidecar.protocol import handle_request

    sample = ROOT / "tests" / "fixtures" / "generated" / "csv_with_markers.csv"

    response = handle_request({"id": "open-markers", "method": "open", "params": {"path": str(sample)}})

    assert response["id"] == "open-markers"
    assert response["ok"] is True
    assert response["result"]["events"] == [
        {"time": 0.02, "label": "stim_a"},
        {"time": 0.07, "label": "stim_b"},
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
