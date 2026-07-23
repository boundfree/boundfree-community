# Third-Party Notices

This file lists the third-party components distributed with BoundFree EEG
Native, their version targets, and license identifiers. Full license texts
are in [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) and, where the
license requires preserving the text verbatim, in
[licenses/](licenses/).

## Native host (C++/Qt6)

| Component   | Version (target) | License                 |
| ----------- | ---------------- | ----------------------- |
| Qt6         | 6.8.x            | LGPL v3 (+ GPL/commercial alternative) |
| pocketfft   | hdronly          | BSD-3-Clause            |

The native host links Qt6 under the GNU LGPL v3. The corresponding source
offer and combined-works notice are in THIRD_PARTY_LICENSES.md.

## Python sidecar (frozen with PyInstaller)

| Component   | Version (target) | License                 |
| ----------- | ---------------- | ----------------------- |
| MNE-Python  | 1.12.x           | BSD-3-Clause            |
| NumPy       | 2.x              | BSD-3-Clause            |
| h5py        | 3.16.x           | BSD-3-Clause            |
| pyedflib    | 0.1.x            | BSD-2-Clause            |
| matplotlib  | 3.10.x           | Matplotlib license (PSF-based) |
| PyInstaller | 6.x              | GPLv2 + linking exception |

### Transitive dependencies of MNE

The frozen sidecar bundles SciPy and pandas as transitive dependencies of
MNE-Python. The sidecar's format readers (`mne`, `h5py`, `pyedflib`,
`numpy`) do not import SciPy or pandas directly; they are included solely
because `collect_all('mne')` preserves MNE's runtime import graph.

| Component | Version (target) | License       |
| --------- | ---------------- | -------------- |
| SciPy     | 1.15.x           | BSD-3-Clause   |
| pandas    | 2.x              | BSD-3-Clause   |

## Components NOT bundled

This release does NOT ship PySide6, pyqtgraph, or cryptography. Those were
part of the legacy Python/PySide6 reference application and are not
distributed with the native C++/Qt6 release. Do not declare them as bundled
dependencies of this product.
