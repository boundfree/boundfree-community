import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_sample_manifest_separates_required_and_sample_gated_formats():
    manifest = json.loads((ROOT / "tests" / "fixtures" / "sample_manifest.json").read_text(encoding="utf-8"))

    required = {entry["format"] for entry in manifest["v1_required_parity"]}
    sample_gated = {entry["format"] for entry in manifest["v1_target_real_sample_gate"]}

    assert required == {
        "EDF/EDF+",
        "BDF",
        "HDF5",
        "CSV",
        "BrainVision",
        "EEGLAB",
        "MNE FIF",
        "Intan RHS",
        "BBK/DAT/LSD",
    }
    assert sample_gated == {"Intan RHD/RHS", "Open Ephys", "Plexon", "Blackrock"}
