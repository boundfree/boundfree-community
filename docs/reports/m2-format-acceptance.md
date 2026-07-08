# M2 Format Acceptance Record

Last updated: 2026-07-08

## Environment Notes

- `h5py` dependency-gated tests skip in this environment when `h5py==3.16.0` is not installed.
- `mne` dependency-gated tests skip in this environment when `mne==1.12.1` is not installed.
- Skipped dependency-gated tests are not format acceptance evidence.
- The acceptance statuses below describe the documented project state, not proof from this environment when a required dependency is missing.

| Format | Status | Evidence |
| --- | --- | --- |
| CSV | Generated fixture automatic tests pass | `tests/sidecar/test_csv_reader.py`, `tests/sidecar/test_reader_contract.py` |
| HDF5 | Dependency-gated generated fixture tests pass when `h5py==3.16.0` is installed; skipped runs in this environment are not acceptance evidence | `tests/sidecar/test_hdf5_reader.py` |
| EDF | Dependency-gated old-repo sample smoke passes when `mne==1.12.1` is installed and `../boundfree-eeg-upper-computer/samples/mark.edf` exists; skipped runs in this environment are not acceptance evidence | `tests/sidecar/test_mne_reader.py` |
| BDF | Reader path exists through MNE, public support claim remains gated by a real or generated acceptance sample | not accepted in this task |
| BrainVision | Reader path exists through MNE, public support claim remains gated by a real or generated acceptance sample | not accepted in this task |
| EEGLAB | Reader path exists through MNE, public support claim remains gated by a real or generated acceptance sample | not accepted in this task |
| MNE FIF | Reader path exists through MNE, public support claim remains gated by a real or generated acceptance sample | not accepted in this task |
| Intan RHS | Not implemented in Native sidecar in this task | not accepted in this task |
| BBK/DAT/LSD | Not implemented in Native sidecar in this task | not accepted in this task |
