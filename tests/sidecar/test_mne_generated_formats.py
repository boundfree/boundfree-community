import importlib.util
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
HAS_MNE = importlib.util.find_spec("mne") is not None
GENERATED = ROOT / "tests" / "fixtures" / "generated"

FORMAT_CASES = [
    ("simple_eeg.edf", "EDF"),
    ("simple_eeg.bdf", "BDF"),
    ("simple_eeg.vhdr", "BrainVision"),
    ("simple_eeg.set", "EEGLAB"),
    ("simple_eeg.fif", "MNE FIF"),
]


@pytest.mark.skipif(not HAS_MNE, reason="mne is not installed")
@pytest.mark.parametrize(("filename", "format_name"), FORMAT_CASES)
def test_mne_generated_fixture_open_and_slice(filename, format_name):
    sample = GENERATED / filename
    if not sample.exists():
        pytest.skip(f"generated fixture missing: {sample}")

    from boundfree_sidecar.protocol import handle_request

    opened = handle_request({"id": f"{format_name}-open", "method": "open", "params": {"path": str(sample)}})
    assert opened["id"] == f"{format_name}-open"
    assert opened["ok"] is True, opened
    assert opened["result"]["format"] == format_name
    assert opened["result"]["fs"] == 100.0
    assert opened["result"]["n_channels"] == 2
    assert opened["result"]["n_samples"] >= 50
    assert opened["result"]["unit"] == "uV"
    assert opened["result"]["ch_names"] == ["ch1", "ch2"]

    handle = opened["result"]["handle"]
    sliced = handle_request(
        {
            "id": f"{format_name}-slice",
            "method": "get_slice",
            "params": {"handle": handle, "t0": 0.0, "t1": 0.1},
        }
    )
    assert sliced["id"] == f"{format_name}-slice"
    assert sliced["ok"] is True, sliced
    assert sliced["result"]["shape"][0] == 2
    assert sliced["result"]["shape"][1] > 0
    assert sliced["result"]["unit"] == "uV"
    assert len(sliced["result"]["data"]) == 2
