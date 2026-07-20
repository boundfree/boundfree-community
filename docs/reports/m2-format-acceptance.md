# M2 Format Acceptance Record

Last updated: 2026-07-21

## Environment Notes

- Optional dependencies for format acceptance in this workspace:
  - `h5py==3.16.0`
  - `mne==1.12.1`
- Export helpers used only to generate fixtures (not runtime product deps):
  - `edfio`, `pybv`, `eeglabio`
- Skipped dependency-gated tests are not format acceptance evidence.
- Generated fixtures live under `tests/fixtures/generated/`.
- Old-repo sample `../boundfree-eeg-upper-computer/samples/mark.edf` remains additional EDF evidence when present.

## Acceptance Status

| Format | Status | Evidence |
| --- | --- | --- |
| CSV | accepted | `tests/sidecar/test_csv_reader.py`, `tests/sidecar/test_reader_contract.py` |
| HDF5 | accepted | `tests/sidecar/test_hdf5_reader.py` (generated fixture; requires `h5py==3.16.0`) |
| EDF | accepted | `tests/sidecar/test_mne_reader.py` (old-repo `mark.edf`) + `tests/sidecar/test_mne_generated_formats.py` (`simple_eeg.edf`) |
| BDF | accepted | `tests/sidecar/test_mne_generated_formats.py` (`simple_eeg.bdf`) |
| BrainVision | accepted | `tests/sidecar/test_mne_generated_formats.py` (`simple_eeg.vhdr` + companion `.eeg/.vmrk`) |
| EEGLAB | accepted | `tests/sidecar/test_mne_generated_formats.py` (`simple_eeg.set`) |
| MNE FIF | accepted | `tests/sidecar/test_mne_generated_formats.py` (`simple_eeg.fif`) |
| Intan RHS | not-implemented | Native sidecar has no RHS reader yet |
| BBK/DAT/LSD | not-implemented | Native sidecar has no BBK/DAT/LSD reader yet |

## Evidence Collection Command

```bash
python3 scripts/collect_m2_format_evidence.py --dry-run
```

Only rows with `status: accepted` and non-empty `evidence` may be reflected as completed checklist items. `dependency-skipped`, `sample-skipped`, and `not-implemented` are not acceptance evidence.

## 2026-07-21 Verification Summary

Commands:

```bash
python3 -m pip install --user 'h5py==3.16.0' 'mne==1.12.1' edfio pybv eeglabio
PYTHONPATH=sidecar python3 -m pytest \
  tests/sidecar/test_hdf5_reader.py \
  tests/sidecar/test_mne_reader.py \
  tests/sidecar/test_mne_generated_formats.py \
  tests/sidecar/test_format_evidence_script.py -q
python3 scripts/collect_m2_format_evidence.py --dry-run
```

Result:

| Format | Status | Notes |
| --- | --- | --- |
| CSV | accepted | generated fixture |
| HDF5 | accepted | generated fixture + h5py |
| EDF | accepted | old-repo sample + generated fixture |
| BDF | accepted | generated fixture |
| BrainVision | accepted | generated fixture |
| EEGLAB | accepted | generated fixture |
| MNE FIF | accepted | generated fixture |
| Intan RHS | not-implemented | deferred beyond M2-5 format path |
| BBK/DAT/LSD | not-implemented | deferred beyond M2-5 format path |

## Public Support Claim Rule

- Generated fixtures prove open/get_slice contract and unit conversion for the listed MNE-backed formats.
- Product/marketing claims for vendor formats still require real-world samples before release notes may say "supported in the field".
- Intan RHS and BBK/DAT/LSD remain not-implemented and must not be claimed.
