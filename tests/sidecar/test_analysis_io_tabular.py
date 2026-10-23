"""Common export formats must enter analysis without unsafe unit/time guesses."""
import csv
from pathlib import Path

import numpy as np
import pytest

from boundfree_sidecar import analysis_io
from boundfree_sidecar.protocol import handle_request
from boundfree_sidecar.writers.export import export_samples


def call(method, **params):
    return handle_request({"id": "tabular", "method": method, "params": params})


@pytest.fixture(autouse=True)
def cleanup():
    yield
    analysis_io.cleanup()


def make_csv(path, names=("a[uV]", "b[mV]"), times=None):
    times = np.arange(1000) / 500 if times is None else times
    values = np.array([np.sin(np.arange(len(times)) / 13), np.cos(np.arange(len(times)) / 19)])
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["time_s", *names])
        writer.writerows([time, *values[:, index]] for index, time in enumerate(times))
    return values


def make_edf(path, units=("uV", "mV"), frequencies=(500, 500)):
    pyedflib = pytest.importorskip("pyedflib")
    arrays = [np.sin(np.arange(2 * fs) / 13) * .7 for fs in frequencies]
    with pyedflib.EdfWriter(str(path), len(units), file_type=pyedflib.FILETYPE_EDFPLUS) as writer:
        writer.setSignalHeaders([{"label": f"ch{index}", "dimension": unit, "sample_frequency": fs,
                                  "physical_min": -2., "physical_max": 2.,
                                  "digital_min": -32768, "digital_max": 32767,
                                  "prefilter": "HP:0.1Hz LP:100Hz", "transducer": ""}
                                 for index, (unit, fs) in enumerate(zip(units, frequencies))])
        writer.writeSamples(arrays)
    return arrays


def test_csv_explicit_units_uniform_time_double_slice(tmp_path):
    path = tmp_path / "explicit.csv"
    data = make_csv(path)
    opened = call("analysis_source_open", path=str(path))
    assert opened["ok"], opened
    result = opened["result"]
    assert result["fs"] == 500 and result["n_samples"] == 1000
    assert result["ch_names"] == ["a", "b"]
    assert result["input_units"] == ["uV", "mV"]
    assert result["input_unit_explicit_by_user"] is False
    read = call("analysis_source_read", handle=result["handle"], first_sample=749, n_samples=251,
                channel_ids=["1", "0"])
    assert read["ok"], read
    np.testing.assert_array_equal(read["result"]["data"], data[[1, 0], 749:] * np.array([[1000], [1]]))


def test_existing_community_csv_export_requires_one_explicit_unit_confirmation(tmp_path):
    path = tmp_path / "existing-export.csv"
    data = np.array([np.arange(1000) / 101, np.arange(1000) / 37], dtype=np.float32)
    export_samples(data, 500., ["Cz", "EOG"], 0., 2., path, [])
    result = call("analysis_source_open", path=str(path))
    assert result["error"]["code"] == "analysis.unknown_unit"
    assert result["error"]["params"]["unit"] == "unknown"
    opened = call("analysis_source_open", path=str(path), csv_unit="uV")
    assert opened["ok"], opened
    result = opened["result"]
    assert result["input_unit_explicit_by_user"] is True and result["csv_unit"] == "uV"
    read = call("analysis_source_read", handle=result["handle"], first_sample=11, n_samples=55, channel_ids=["0", "1"])
    np.testing.assert_array_equal(read["result"]["data"], data[:, 11:66].astype(np.float64))
    assert analysis_io.source_registry().sources[result["handle"]].reader.__dict__.keys().isdisjoint({"_rows", "data"})


@pytest.mark.parametrize("unit,factor", [("uV", 1), ("mV", 1000), ("V", 1e6)])
def test_csv_confirmed_unit_converts_and_survives_metadata(tmp_path, unit, factor):
    path = tmp_path / "declared.csv"
    data = make_csv(path, names=("left", "right"))
    result = call("analysis_source_open", path=str(path), csv_unit=unit)["result"]
    read = call("analysis_source_read", handle=result["handle"], first_sample=4, n_samples=3, channel_ids=["1"])
    np.testing.assert_array_equal(read["result"]["data"], data[1:2, 4:7] * factor)
    assert result["csv_unit"] == unit


@pytest.mark.parametrize("times", [np.arange(1000) / 500 + 1.,
                                   np.r_[np.arange(400) / 500, (np.arange(600) + 401) / 500],
                                   np.arange(1000) / 500 + np.sin(np.arange(1000)) * 1e-5,
                                   np.r_[np.arange(400) / 500, np.arange(600) / 500],
                                   np.array([0.]), np.array([0., float("nan")])])
def test_csv_nonzero_irregular_gaps_duplicates_and_short_axis_rejected(tmp_path, times):
    path = tmp_path / "invalid.csv"
    make_csv(path, times=times)
    response = call("analysis_source_open", path=str(path))
    assert not response["ok"]
    assert response["error"]["code"] in ("analysis.source_invalid", "analysis.discontinuous_time")


@pytest.mark.parametrize("names,override", [(("a[counts]", "b[uV]"), None), (("a[nV]", "b[nV]"), "uV"),
                                          (("a", "b"), "counts")])
def test_csv_unknown_units_never_assumed_or_overwritten(tmp_path, names, override):
    path = tmp_path / "unknown.csv"
    make_csv(path, names=names)
    response = call("analysis_source_open", path=str(path), **({"csv_unit": override} if override else {}))
    assert response["error"]["code"] == "analysis.unknown_unit"


def test_edf_explicit_units_slice_matches_physical_calibration(tmp_path):
    pyedflib = pytest.importorskip("pyedflib")
    path = tmp_path / "known.edf"
    make_edf(path)
    opened = call("analysis_source_open", path=str(path))
    assert opened["ok"], opened
    result = opened["result"]
    assert result["fs"] == 500 and result["n_samples"] == 1000
    read = call("analysis_source_read", handle=result["handle"], first_sample=797, n_samples=203,
                channel_ids=["1", "0"])
    assert read["ok"], read
    with pyedflib.EdfReader(str(path)) as source:
        expected = np.array([source.readSignal(1, start=797, n=203) * 1000,
                             source.readSignal(0, start=797, n=203)])
    np.testing.assert_array_equal(read["result"]["data"], expected)
    assert result["hardware"]["lower_bandwidth_hz"] is None
    assert result["hardware"]["prefilter_text"] == ["HP:0.1Hz LP:100Hz"] * 2


def test_existing_community_edf_export_roundtrip_to_analysis(tmp_path):
    pytest.importorskip("pyedflib")
    path = tmp_path / "community.edf"
    data = np.array([np.sin(np.arange(1000) / 11) * 90, np.cos(np.arange(1000) / 19) * 120], dtype=np.float32)
    export_samples(data, 500., ["EEG", "EOG"], 0, 2, path, [])
    opened = call("analysis_source_open", path=str(path))
    assert opened["ok"], opened
    read = call("analysis_source_read", handle=opened["result"]["handle"], first_sample=10,
                n_samples=940, channel_ids=["0", "1"])
    assert read["ok"], read
    np.testing.assert_allclose(read["result"]["data"], data[:, 10:950], atol=6400 / 65535)


@pytest.mark.parametrize("case", ["different-fs", "unknown-unit", "EDF+D"])
def test_edf_mixed_rates_unknown_units_and_discontinuous_flag_rejected(tmp_path, case):
    path = tmp_path / "invalid.edf"
    if case == "different-fs":
        make_edf(path, frequencies=(500, 250))
    elif case == "unknown-unit":
        make_edf(path, units=("uV", "counts"))
    else:
        make_edf(path)
        with path.open("r+b") as stream:
            stream.seek(192)
            stream.write(b"EDF+D")
    response = call("analysis_source_open", path=str(path))
    assert not response["ok"]
    assert response["error"]["code"] == {"different-fs": "analysis.source_invalid", "unknown-unit": "analysis.unknown_unit",
                                          "EDF+D": "analysis.discontinuous_time"}[case]


@pytest.mark.parametrize("suffix", ["csv", "edf"])
def test_tabular_boundaries_and_resource_check_happen_before_slice(tmp_path, monkeypatch, suffix):
    import importlib
    module = importlib.import_module("boundfree_sidecar.analysis_io.sources")
    path = tmp_path / ("source." + suffix)
    if suffix == "csv":
        make_csv(path)
    else:
        make_edf(path)
    opened = call("analysis_source_open", path=str(path))["result"]
    for start, count, channels in [(1000, 1, ["0"]), (-1, 10, ["0"]), (0, 10, ["1", "1"])]:
        response = call("analysis_source_read", handle=opened["handle"], first_sample=start,
                        n_samples=count, channel_ids=channels)
        assert response["error"]["code"] == "analysis.invalid_request"
    snapshot = analysis_io.source_registry().sources[opened["handle"]]
    monkeypatch.setattr(snapshot.reader, "read", lambda *a: pytest.fail("resource check must precede read/allocation"))
    monkeypatch.setattr(module, "MAX_BYTES", 15)
    response = call("analysis_source_read", handle=opened["handle"], first_sample=0, n_samples=2, channel_ids=["0"])
    assert response["error"]["code"] == "analysis.resource_limit"
