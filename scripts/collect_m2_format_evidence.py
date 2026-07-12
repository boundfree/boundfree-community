#!/usr/bin/env python3
import argparse
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EDF_SAMPLE = ROOT.parent / "boundfree-eeg-upper-computer" / "samples" / "mark.edf"


def dependency_available(name: str) -> bool:
    return importlib.util.find_spec(name) is not None


def collect() -> dict[str, dict[str, object]]:
    h5py_available = dependency_available("h5py")
    mne_available = dependency_available("mne")
    edf_sample_available = EDF_SAMPLE.exists()

    return {
        "CSV": {
            "status": "accepted",
            "evidence": ["tests/sidecar/test_csv_reader.py", "tests/sidecar/test_reader_contract.py"],
            "skip_is_acceptance_evidence": False,
        },
        "HDF5": {
            "status": "accepted" if h5py_available else "dependency-skipped",
            "evidence": ["tests/sidecar/test_hdf5_reader.py"] if h5py_available else [],
            "required_dependency": "h5py==3.16.0",
            "skip_is_acceptance_evidence": False,
        },
        "EDF": {
            "status": "accepted"
            if (mne_available and edf_sample_available)
            else ("dependency-skipped" if not mne_available else "sample-skipped"),
            "evidence": ["tests/sidecar/test_mne_reader.py"] if (mne_available and edf_sample_available) else [],
            "required_dependency": "mne==1.12.1",
            "sample": str(EDF_SAMPLE),
            "skip_is_acceptance_evidence": False,
        },
        "BDF": {"status": "sample-skipped", "evidence": [], "skip_is_acceptance_evidence": False},
        "BrainVision": {"status": "sample-skipped", "evidence": [], "skip_is_acceptance_evidence": False},
        "EEGLAB": {"status": "sample-skipped", "evidence": [], "skip_is_acceptance_evidence": False},
        "MNE FIF": {"status": "sample-skipped", "evidence": [], "skip_is_acceptance_evidence": False},
        "Intan RHS": {"status": "not-implemented", "evidence": [], "skip_is_acceptance_evidence": False},
        "BBK/DAT/LSD": {"status": "not-implemented", "evidence": [], "skip_is_acceptance_evidence": False},
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    parser.parse_args()
    print(json.dumps(collect(), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
