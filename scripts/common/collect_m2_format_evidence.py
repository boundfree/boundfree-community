#!/usr/bin/env python3
import argparse
import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
_EDF_CANDIDATE = ROOT.parent / "boundfree-eeg-upper-computer" / "samples" / "mark.edf"
_EDF_ARCHIVE = ROOT.parent / "archive" / "boundfree-eeg-upper-computer" / "samples" / "mark.edf"
EDF_SAMPLE = _EDF_CANDIDATE if _EDF_CANDIDATE.exists() else _EDF_ARCHIVE
GENERATED = ROOT / "tests" / "fixtures" / "generated"


def dependency_available(name: str) -> bool:
    return importlib.util.find_spec(name) is not None


def generated_exists(name: str) -> bool:
    return (GENERATED / name).exists()


def collect() -> dict[str, dict[str, object]]:
    h5py_available = dependency_available("h5py")
    mne_available = dependency_available("mne")
    edf_sample_available = EDF_SAMPLE.exists()
    generated_edf = generated_exists("simple_eeg.edf")
    generated_bdf = generated_exists("simple_eeg.bdf")
    generated_vhdr = generated_exists("simple_eeg.vhdr")
    generated_set = generated_exists("simple_eeg.set")
    generated_fif = generated_exists("simple_eeg.fif")

    edf_accepted = mne_available and (edf_sample_available or generated_edf)
    edf_evidence = []
    if edf_accepted:
        if edf_sample_available:
            edf_evidence.append("tests/sidecar/test_mne_reader.py")
        if generated_edf:
            edf_evidence.append("tests/sidecar/test_mne_generated_formats.py")

    def mne_generated(status_name: str, filename: str) -> dict[str, object]:
        if not mne_available:
            return {
                "status": "dependency-skipped",
                "evidence": [],
                "required_dependency": "mne==1.12.1",
                "skip_is_acceptance_evidence": False,
            }
        if not generated_exists(filename):
            return {
                "status": "sample-skipped",
                "evidence": [],
                "sample": str(GENERATED / filename),
                "skip_is_acceptance_evidence": False,
            }
        return {
            "status": "accepted",
            "evidence": ["tests/sidecar/test_mne_generated_formats.py"],
            "sample": str(GENERATED / filename),
            "skip_is_acceptance_evidence": False,
        }

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
            if edf_accepted
            else ("dependency-skipped" if not mne_available else "sample-skipped"),
            "evidence": edf_evidence,
            "required_dependency": "mne==1.12.1",
            "sample": str(EDF_SAMPLE if edf_sample_available else GENERATED / "simple_eeg.edf"),
            "skip_is_acceptance_evidence": False,
        },
        "BDF": mne_generated("BDF", "simple_eeg.bdf"),
        "BrainVision": mne_generated("BrainVision", "simple_eeg.vhdr"),
        "EEGLAB": mne_generated("EEGLAB", "simple_eeg.set"),
        "MNE FIF": mne_generated("MNE FIF", "simple_eeg.fif"),
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
