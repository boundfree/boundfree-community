# Test Fixtures

This directory stores generated fixtures and real-sample manifest metadata for BoundFree EEG Native.

`sample_manifest.json` separates v1 required parity formats from v1 target formats that need a real-sample gate before public support claims.

Generated fixtures under `generated/` may be committed:

- CSV: `simple_eeg.csv`, marker sidecars
- HDF5: created on the fly by tests when `h5py` is installed
- MNE-backed: `simple_eeg.edf`, `simple_eeg.bdf`, `simple_eeg.vhdr` (+ `.eeg`/`.vmrk`), `simple_eeg.set`, `simple_eeg.fif`

Real customer or vendor samples must be stored according to the project data policy and referenced by manifest entry, not copied into the repository without approval.
