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


def test_csv_open_no_samples_rejects_empty_data(tmp_path):
    """A CSV with header but zero data rows returns csv.no_samples."""
    from boundfree_sidecar.protocol import handle_request

    bad_csv = tmp_path / "bad.csv"
    bad_csv.write_text("time_s,ch1\n", encoding="utf-8")

    response = handle_request({"id": "open-nosamples", "method": "open", "params": {"path": str(bad_csv)}})

    assert response["id"] == "open-nosamples"
    assert response["ok"] is False
    assert response["error"]["code"] == "csv.no_samples"
    assert response["error"]["params"]["path"] == str(bad_csv)


def test_open_marker_table_csv_rejects_with_content_based_detection(tmp_path):
    """CSV with time_s,label headers is rejected as csv.marker_table regardless of filename."""
    from boundfree_sidecar.protocol import handle_request

    sidecar = tmp_path / "annotations.csv"
    sidecar.write_text("time_s,label\n0.02,stim_a\n0.07,stim_b\n", encoding="utf-8")

    response = handle_request({"id": "open-marker", "method": "open", "params": {"path": str(sidecar)}})

    assert response["id"] == "open-marker"
    assert response["ok"] is False
    assert response["error"]["code"] == "csv.marker_table"
    assert response["error"]["params"]["path"] == str(sidecar)


def test_csv_open_missing_time_column(tmp_path):
    """CSV missing time_s column returns csv.missing_time_column."""
    from boundfree_sidecar.protocol import handle_request

    bad = tmp_path / "bad.csv"
    bad.write_text("ch1,ch2\n0.0,10.0\n1.0,11.0\n", encoding="utf-8")

    response = handle_request({"id": "open-notime", "method": "open", "params": {"path": str(bad)}})

    assert response["id"] == "open-notime"
    assert response["ok"] is False
    assert response["error"]["code"] == "csv.missing_time_column"
    assert response["error"]["params"]["path"] == str(bad)


def test_csv_open_no_signal_columns(tmp_path):
    """CSV with time_s but no signal columns returns csv.no_signal_columns."""
    from boundfree_sidecar.protocol import handle_request

    bad = tmp_path / "bad.csv"
    bad.write_text("time_s\n0.00\n0.01\n", encoding="utf-8")

    response = handle_request({"id": "open-nosig", "method": "open", "params": {"path": str(bad)}})

    assert response["id"] == "open-nosig"
    assert response["ok"] is False
    assert response["error"]["code"] == "csv.no_signal_columns"
    assert response["error"]["params"]["path"] == str(bad)


def test_csv_open_invalid_signal_value(tmp_path):
    """Non-numeric signal cell returns csv.invalid_signal_value with column and row."""
    from boundfree_sidecar.protocol import handle_request

    bad = tmp_path / "bad.csv"
    bad.write_text("time_s,ch1,ch2\n0.00,0.0,10.0\n0.01,1.0,NaN\n", encoding="utf-8")

    response = handle_request({"id": "open-badsig", "method": "open", "params": {"path": str(bad)}})

    assert response["id"] == "open-badsig"
    assert response["ok"] is False
    assert response["error"]["code"] == "csv.invalid_signal_value"
    assert response["error"]["params"]["path"] == str(bad)
    assert response["error"]["params"]["column"] == "ch2"
    assert response["error"]["params"]["row"] == 2


def test_csv_open_invalid_time_axis(tmp_path):
    """Non-strictly-increasing or non-numeric time_s returns csv.invalid_time_axis."""
    from boundfree_sidecar.protocol import handle_request

    bad = tmp_path / "bad.csv"
    bad.write_text("time_s,ch1\n0.00,0.0\n0.00,1.0\n", encoding="utf-8")

    response = handle_request({"id": "open-badtime", "method": "open", "params": {"path": str(bad)}})

    assert response["id"] == "open-badtime"
    assert response["ok"] is False
    assert response["error"]["code"] == "csv.invalid_time_axis"
    assert response["error"]["params"]["path"] == str(bad)


def test_csv_open_custom_channel_names(tmp_path):
    """CSV with non-chN channel names opens successfully."""
    from boundfree_sidecar.protocol import handle_request

    good = tmp_path / "good.csv"
    good.write_text("time_s,alpha,beta\n0.00,1.0,2.0\n0.01,3.0,4.0\n", encoding="utf-8")

    response = handle_request({"id": "open-custom", "method": "open", "params": {"path": str(good)}})

    assert response["id"] == "open-custom"
    assert response["ok"] is True
    assert response["result"]["ch_names"] == ["alpha", "beta"]
    assert response["result"]["n_channels"] == 2
    assert response["result"]["n_samples"] == 2


def test_csv_open_marker_sidecar_events_with_non_marker_filename(tmp_path):
    """Marker sidecar is loaded by stem-matching, not by content — the data file is not a marker table."""
    from boundfree_sidecar.protocol import handle_request

    eeg = tmp_path / "session_data.csv"
    eeg.write_text("time_s,ch1\n0.00,0.0\n0.01,1.0\n", encoding="utf-8")
    markers = tmp_path / "session_data_markers.csv"
    markers.write_text("time_s,label\n0.005,stim_a\n0.015,stim_b\n", encoding="utf-8")

    response = handle_request({"id": "open-session", "method": "open", "params": {"path": str(eeg)}})

    assert response["id"] == "open-session"
    assert response["ok"] is True
    assert response["result"]["events"] == [
        {"time": 0.005, "label": "stim_a"},
        {"time": 0.015, "label": "stim_b"},
    ]
