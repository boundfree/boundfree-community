# Public EEG Samples

Files under `data/` are local test assets and are intentionally excluded from Git.

## Sources

| Local file | Format | Source | License / terms |
| --- | --- | --- | --- |
| `data/physionet_eegmmidb_S001R01.edf` | EDF, 64-channel EEG | PhysioNet EEG Motor Movement/Imagery Dataset, `S001/S001R01.edf` | Open Data Commons Attribution License v1.0; cite the dataset publications listed by PhysioNet |
| `data/pyedflib_test_generator.edf` | EDF test signal | pyEDFlib repository, `pyedflib/tests/data/test_generator.edf` | pyEDFlib repository license; use as software test data |
| `data/pyedflib_test_generator.bdf` | BDF test signal | pyEDFlib repository, `pyedflib/tests/data/test_generator.bdf` | pyEDFlib repository license; use as software test data |
| `data/mne_brainvision/test.vhdr` plus `.vmrk` and `.eeg` | BrainVision test signal | MNE-Python repository, `mne/io/brainvision/tests/data/test.*` | MNE-Python BSD 3-Clause license; use as software test data |
| `data/uci_eeg_eye_state.arff` | ARFF, 14-channel EEG plus eye-state label | UCI Machine Learning Repository, EEG Eye State dataset (dataset 264) | Creative Commons Attribution 4.0 International |
| `data/uci_eeg_eye_state.csv` | CSV derived from the UCI ARFF file | Converted locally: adds `time_s` at 128 Hz, excludes the classification label, and removes each channel mean | Same CC BY 4.0 terms as the source dataset |
| `data/uci_eeg_eye_state.h5` | BoundFree-compatible HDF5 derived from the UCI ARFF file | Same centered values stored as dataset `eeg`, shape `[channels, samples]`, 128 Hz | Same CC BY 4.0 terms as the source dataset |

## Source URLs

- <https://physionet.org/files/eegmmidb/1.0.0/S001/S001R01.edf>
- <https://physionet.org/content/eegmmidb/1.0.0/>
- <https://raw.githubusercontent.com/holgern/pyedflib/master/pyedflib/tests/data/test_generator.edf>
- <https://raw.githubusercontent.com/holgern/pyedflib/master/pyedflib/tests/data/test_generator.bdf>
- <https://github.com/holgern/pyedflib>
- <https://raw.githubusercontent.com/mne-tools/mne-python/main/mne/io/brainvision/tests/data/test.vhdr>
- <https://raw.githubusercontent.com/mne-tools/mne-python/main/mne/io/brainvision/tests/data/test.vmrk>
- <https://raw.githubusercontent.com/mne-tools/mne-python/main/mne/io/brainvision/tests/data/test.eeg>
- <https://github.com/mne-tools/mne-python>
- <https://archive.ics.uci.edu/dataset/264/eeg+eye+state>
- <https://archive.ics.uci.edu/static/public/264/eeg+eye+state.zip>

These assets are for interoperability testing, not redistribution as BoundFree-owned data.

The UCI dataset documentation does not establish that its numeric values are physical microvolts. The derived CSV/HDF5 files are centered and tagged `uV` only to exercise the current BoundFree reader contract. Do not use them for scientific amplitude validation.
