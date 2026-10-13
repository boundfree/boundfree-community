import importlib.util
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
HAS_MNE = importlib.util.find_spec("mne") is not None
_EDF_CANDIDATE = ROOT.parent / "boundfree-eeg-upper-computer" / "samples" / "mark.edf"
_EDF_ARCHIVE = ROOT.parent / "archive" / "boundfree-eeg-upper-computer" / "samples" / "mark.edf"
EDF_SAMPLE = _EDF_CANDIDATE if _EDF_CANDIDATE.exists() else _EDF_ARCHIVE


@pytest.mark.skipif(not HAS_MNE, reason="mne is not installed")
@pytest.mark.skipif(not EDF_SAMPLE.exists(), reason="old-repo mark.edf sample is unavailable")
def test_mne_edf_reader_opens_old_repo_sample():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "edf-open", "method": "open", "params": {"path": str(EDF_SAMPLE)}})

    assert response["id"] == "edf-open"
    assert response["ok"] is True
    assert response["result"]["format"] == "EDF"
    assert response["result"]["fs"] > 0
    assert response["result"]["n_channels"] > 0
    assert response["result"]["n_samples"] > 0
    assert response["result"]["duration"] > 0
    assert response["result"]["unit"] == "uV"


@pytest.mark.skipif(not HAS_MNE, reason="mne is not installed")
@pytest.mark.skipif(not EDF_SAMPLE.exists(), reason="old-repo mark.edf sample is unavailable")
def test_mne_edf_reader_get_slice_returns_uv_window():
    from boundfree_sidecar.protocol import handle_request

    opened = handle_request({"id": "edf-open-2", "method": "open", "params": {"path": str(EDF_SAMPLE)}})
    handle = opened["result"]["handle"]

    response = handle_request({"id": "edf-slice", "method": "get_slice", "params": {"handle": handle, "t0": 0.0, "t1": 0.1}})

    assert response["id"] == "edf-slice"
    assert response["ok"] is True
    assert response["result"]["shape"][0] == opened["result"]["n_channels"]
    assert response["result"]["shape"][1] > 0
    assert response["result"]["unit"] == "uV"
