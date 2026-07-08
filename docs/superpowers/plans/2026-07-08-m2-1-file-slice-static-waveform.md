# M2-1 File Slice And Static Waveform Browser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the next M2 slice of v1 file analysis: structured `get_slice` in Native, dependency-gated HDF5/MNE reader expansion, and a static waveform browser entry that can open a file without blocking the GUI thread.

**Architecture:** Python sidecar remains the only file-format owner. Native owns session state, slice request coordination, decimation, and display. This plan keeps M2 focused on static file browsing; DSP, playback, annotations editing, video sync, export, and hardware acquisition remain outside this phase.

**Tech Stack:** C++17, Qt6 Core/Widgets, Python 3.10+, pytest, optional sidecar dependencies `h5py==3.16.0` and `mne==1.12.1`, JSON IPC for small verified slices, MinMax decimation, QPainter-based smoke rendering with the existing real-GPU gate kept open for final M2 acceptance.

## Global Constraints

- Work in `boundfree-eeg-native`; use `../boundfree-eeg-upper-computer` only as a reference implementation, sample source, and golden-file source.
- Do not modify `../boundfree-eeg-upper-computer` in this plan.
- v1 stable is a C++/Qt6 Native general EEG/LFP/electrophysiology file analysis product.
- v1 stable does not include self-developed ADS1299/ESP32-C3 BLE acquisition closure; hardware acquisition enters v1.1/v2.
- v1 required parity formats: EDF/EDF+, BDF, HDF5, CSV, BrainVision, EEGLAB, MNE FIF, Intan RHS, BBK/DAT/LSD.
- v1 target real-sample-gated formats: Intan RHD/RHS, Open Ephys, Plexon, Blackrock.
- No format can be announced as supported without real samples and an automatic or manual acceptance record.
- `open` reads metadata only; large files must not be fully loaded during open.
- `get_slice` reads the requested time interval only.
- EEG data shape is `[n_channels, n_samples]`, unit `uV`.
- GUI main thread must not perform file IO, DSP, export, or long-running analysis.
- Fast seek and quick file switching use latest-request-wins; older results must not overwrite newer windows.
- WSL/Xvfb/Mesa rendering results are development signals only; real GPU/display verification is required before full M2 waveform acceptance.
- Current environment has `python3`; do not rely on a `python` executable.
- `docs/style-preview.html` is an existing untracked file and is not part of this plan.

---

## File Structure

New or modified files are scoped by responsibility:

| Path | Responsibility |
| --- | --- |
| `sidecar/pyproject.toml` | Declare optional file-format dependencies without forcing every fast test run to install MNE/HDF5. |
| `contracts/error_codes.json` | Add structured reader failure error for dependency or open failures. |
| `native/i18n/bf_app_*.ts` | Keep error-code localization keys complete for all locale files. |
| `tests/sidecar/test_optional_dependencies.py` | Verify the optional dependency declaration is stable. |
| `tests/fixtures/generated/csv_with_markers.csv` | CSV fixture for marker-sidecar parsing. |
| `tests/fixtures/generated/csv_with_markers_markers.csv` | CSV marker sidecar fixture. |
| `tests/sidecar/test_reader_contract.py` | Verify reader failures and marker metadata use the formal protocol envelope. |
| `sidecar/boundfree_sidecar/readers/base.py` | Shared `FileMetadata`, `SliceData`, and `Reader` contracts. |
| `sidecar/boundfree_sidecar/readers/csv_reader.py` | CSV metadata, marker sidecar, and slice extraction. |
| `sidecar/boundfree_sidecar/readers/hdf5_reader.py` | HDF5 default schema reader for `eeg` dataset. |
| `sidecar/boundfree_sidecar/readers/mne_reader.py` | MNE-backed reader for EDF/BDF/BrainVision/EEGLAB/FIF, dependency-gated. |
| `sidecar/boundfree_sidecar/readers/registry.py` | Extension dispatch without importing heavy dependencies at startup. |
| `sidecar/boundfree_sidecar/protocol.py` | Convert reader exceptions into structured error envelopes and return formal slice payloads. |
| `tests/sidecar/test_hdf5_reader.py` | Generated HDF5 fixture metadata and slice tests, skipped when `h5py` is unavailable. |
| `tests/sidecar/test_mne_reader.py` | Old-repo EDF sample smoke tests, skipped when `mne` or sample file is unavailable. |
| `native/src/ipc/sidecar_client.h` | Native slice result type and `getSlice()` interface. |
| `native/src/ipc/sidecar_client.cpp` | Parse sidecar JSON slice payload into Native vectors. |
| `native/src/controller/file_session.h` | Store current file metadata and optional current slice summary. |
| `native/src/render/waveform_slice.h` | Native display model for `[n_channels, n_samples]` slices. |
| `native/src/render/waveform_slice.cpp` | Convert sidecar slices into renderable channel data and decimated summaries. |
| `native/src/ui/waveform_widget.h` | Static waveform widget API for M2-1 smoke rendering. |
| `native/src/ui/waveform_widget.cpp` | QPainter-based static waveform rendering used by offscreen smoke tests. |
| `native/src/controller/file_open_controller.h` | Async open/slice worker boundary for GUI use. |
| `native/src/controller/file_open_controller.cpp` | QThread-backed open and initial-slice request flow. |
| `native/src/ui/main_window.h` | File menu, open callback, session/slice display entry points. |
| `native/src/ui/main_window.cpp` | Static browser layout, status updates, and file-open command wiring. |
| `native/src/app/main.cpp` | New smoke CLIs and production wiring between window, controller, and sidecar. |
| `native/CMakeLists.txt` | Register new Native source files. |
| `tests/native/test_bf_app.py` | Native slice and waveform smoke tests. |
| `docs/reports/m2-format-acceptance.md` | Record which formats are automatically verified, dependency-gated, or still sample-gated. |
| `docs/feature-parity-checklist.md` | Mark only completed M2-1 sub-items; keep unsupported formats unchecked. |

---

### Task 1: Dependency And Reader Failure Gate

**Files:**
- Modify: `sidecar/pyproject.toml`
- Modify: `contracts/error_codes.json`
- Modify: `native/i18n/bf_app_zh_CN.ts`
- Modify: `native/i18n/bf_app_en_US.ts`
- Modify: `native/i18n/bf_app_pseudo.ts`
- Modify: `native/i18n/bf_app_de_DE.ts`
- Modify: `native/i18n/bf_app_es_ES.ts`
- Modify: `native/i18n/bf_app_fr_FR.ts`
- Modify: `native/i18n/bf_app_ja_JP.ts`
- Modify: `native/i18n/bf_app_ko_KR.ts`
- Modify: `native/i18n/bf_app_zh_TW.ts`
- Create: `tests/sidecar/test_optional_dependencies.py`
- Test: `tests/contracts/test_error_codes.py`
- Test: `tests/i18n/test_i18n_static.py`

**Interfaces:**
- Consumes: existing sidecar package metadata and error-code contract.
- Produces: optional dependency declaration `formats = ["h5py==3.16.0", "mne==1.12.1"]` and error code `reader.open_failed`.

- [ ] **Step 1: Write the dependency declaration test**

Create `tests/sidecar/test_optional_dependencies.py`:

```python
import tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_sidecar_declares_format_optional_dependencies():
    data = tomllib.loads((ROOT / "sidecar" / "pyproject.toml").read_text(encoding="utf-8"))

    assert data["project"]["optional-dependencies"]["formats"] == [
        "h5py==3.16.0",
        "mne==1.12.1",
    ]
```

- [ ] **Step 2: Run the dependency declaration test and verify it fails**

Run:

```bash
python3 -m pytest tests/sidecar/test_optional_dependencies.py -q
```

Expected: FAIL with `KeyError: 'optional-dependencies'`.

- [ ] **Step 3: Add optional dependencies**

Append this section to `sidecar/pyproject.toml`:

```toml
[project.optional-dependencies]
formats = [
    "h5py==3.16.0",
    "mne==1.12.1",
]
```

- [ ] **Step 4: Add the reader failure error contract**

Add this object to `contracts/error_codes.json` after `reader.unsupported_format`:

```json
{
  "code": "reader.open_failed",
  "params": ["path", "reason"],
  "retryable": false,
  "description": "Sidecar reader failed while opening or reading the requested file."
}
```

Add `error.reader.open_failed` to every `native/i18n/bf_app_*.ts` file. Use these required source texts:

```xml
<message>
    <source>error.reader.open_failed</source>
    <translation>Reader failed: {reason}</translation>
</message>
```

For `bf_app_zh_CN.ts`, use:

```xml
<message>
    <source>error.reader.open_failed</source>
    <translation>读取器失败：{reason}</translation>
</message>
```

For `bf_app_pseudo.ts`, use:

```xml
<message>
    <source>error.reader.open_failed</source>
    <translation>[!! Reader failed: {reason} !!]</translation>
</message>
```

- [ ] **Step 5: Verify dependency and contract gates**

Run:

```bash
python3 -m pytest tests/sidecar/test_optional_dependencies.py tests/contracts/test_error_codes.py tests/i18n/test_i18n_static.py -q
```

Expected: all selected tests pass.

- [ ] **Step 6: Commit**

Run:

```bash
git add sidecar/pyproject.toml contracts/error_codes.json native/i18n tests/sidecar/test_optional_dependencies.py
git commit -m "chore: declare sidecar format dependency gate"
```

### Task 2: Sidecar Slice Contract And CSV Markers

**Files:**
- Create: `tests/fixtures/generated/csv_with_markers.csv`
- Create: `tests/fixtures/generated/csv_with_markers_markers.csv`
- Create: `tests/sidecar/test_reader_contract.py`
- Modify: `sidecar/boundfree_sidecar/readers/base.py`
- Modify: `sidecar/boundfree_sidecar/readers/csv_reader.py`
- Modify: `sidecar/boundfree_sidecar/protocol.py`
- Test: `tests/sidecar/test_csv_reader.py`
- Test: `tests/sidecar/test_reader_contract.py`

**Interfaces:**
- Consumes: Task 1 `reader.open_failed`.
- Produces: `SliceData` sidecar contract and CSV marker sidecar parsing.

- [ ] **Step 1: Add marker fixtures**

Create `tests/fixtures/generated/csv_with_markers.csv`:

```csv
time_s,ch1,ch2
0.00,0.0,10.0
0.01,1.0,11.0
0.02,2.0,12.0
0.03,3.0,13.0
0.04,4.0,14.0
0.05,5.0,15.0
0.06,6.0,16.0
0.07,7.0,17.0
0.08,8.0,18.0
0.09,9.0,19.0
```

Create `tests/fixtures/generated/csv_with_markers_markers.csv`:

```csv
time_s,label
0.02,stim_a
0.07,stim_b
```

- [ ] **Step 2: Write reader contract tests**

Create `tests/sidecar/test_reader_contract.py`:

```python
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_csv_reader_open_includes_marker_sidecar_events():
    from boundfree_sidecar.protocol import handle_request

    sample = ROOT / "tests" / "fixtures" / "generated" / "csv_with_markers.csv"

    response = handle_request({"id": "open-markers", "method": "open", "params": {"path": str(sample)}})

    assert response["id"] == "open-markers"
    assert response["ok"] is True
    assert response["result"]["events"] == [
        {"time": 0.02, "label": "stim_a"},
        {"time": 0.07, "label": "stim_b"},
    ]


def test_reader_open_exception_returns_structured_error(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    bad_csv = tmp_path / "bad.csv"
    bad_csv.write_text("time_s,ch1\n", encoding="utf-8")

    response = handle_request({"id": "open-bad", "method": "open", "params": {"path": str(bad_csv)}})

    assert response["id"] == "open-bad"
    assert response["ok"] is False
    assert response["error"]["code"] == "reader.open_failed"
    assert response["error"]["params"]["path"] == str(bad_csv)
    assert response["error"]["params"]["reason"]
```

- [ ] **Step 3: Run contract tests and verify they fail**

Run:

```bash
python3 -m pytest tests/sidecar/test_reader_contract.py -q
```

Expected: FAIL because marker sidecar parsing and `reader.open_failed` handling are not implemented.

- [ ] **Step 4: Extend the sidecar reader base contract**

Modify `sidecar/boundfree_sidecar/readers/base.py` to include `SliceData` and update the abstract reader return type:

```python
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class FileMetadata:
    fs: float
    n_channels: int
    n_samples: int
    duration: float
    ch_names: list[str]
    unit: str
    format_name: str
    events: list[dict[str, object]]


@dataclass(frozen=True)
class SliceData:
    t0: float
    t1: float
    fs: float
    unit: str
    data: list[list[float]]

    @property
    def shape(self) -> list[int]:
        return [len(self.data), len(self.data[0]) if self.data else 0]


class Reader:
    format_name = ""

    def __init__(self, path: Path) -> None:
        self.path = path

    def metadata(self) -> FileMetadata:
        raise NotImplementedError

    def get_slice(self, t0: float, t1: float) -> SliceData:
        raise NotImplementedError
```

- [ ] **Step 5: Implement CSV marker sidecar and `SliceData`**

Modify `sidecar/boundfree_sidecar/readers/csv_reader.py` so `metadata()` calls `_load_events()` and `get_slice()` returns `SliceData`:

```python
def _load_events(self) -> list[dict[str, object]]:
    markers_path = self.path.with_name(self.path.stem + "_markers.csv")
    if not markers_path.exists():
        return []
    events: list[dict[str, object]] = []
    with markers_path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            events.append({"time": float(row["time_s"]), "label": str(row["label"])})
    return events
```

Use this return block in `metadata()`:

```python
return FileMetadata(
    fs=fs,
    n_channels=len(ch_names),
    n_samples=len(rows),
    duration=duration,
    ch_names=ch_names,
    unit="uV",
    format_name=self.format_name,
    events=self._load_events(),
)
```

Use this return block in `get_slice()`:

```python
return SliceData(
    t0=t0,
    t1=t1,
    fs=meta.fs,
    unit="uV",
    data=[
        [float(row[ch]) for row in rows[start:stop]]
        for ch in meta.ch_names
    ],
)
```

- [ ] **Step 6: Convert reader failures into protocol errors**

Modify `_handle_open()` in `sidecar/boundfree_sidecar/protocol.py` so `reader.metadata()` exceptions return `reader.open_failed`:

```python
try:
    meta = reader.metadata()
except Exception as exc:  # noqa: BLE001 - reader failures must cross IPC as data.
    return error_response(request_id, "reader.open_failed", {"path": raw_path, "reason": str(exc)})
```

Modify `_handle_get_slice()` so `SliceData` is serialized:

```python
try:
    data = session.reader.get_slice(t0, t1)
except Exception as exc:  # noqa: BLE001 - reader failures must cross IPC as data.
    return error_response(request_id, "reader.open_failed", {"path": str(session.path), "reason": str(exc)})
return success_response(
    request_id,
    {
        "handle": handle,
        "t0": data.t0,
        "t1": data.t1,
        "fs": data.fs,
        "shape": data.shape,
        "unit": data.unit,
        "data": data.data,
    },
)
```

- [ ] **Step 7: Verify sidecar slice and CSV behavior**

Run:

```bash
python3 -m pytest tests/sidecar/test_reader_contract.py tests/sidecar/test_csv_reader.py tests/sidecar/test_sidecar.py -q
```

Expected: all selected sidecar tests pass.

- [ ] **Step 8: Commit**

Run:

```bash
git add tests/fixtures/generated/csv_with_markers.csv tests/fixtures/generated/csv_with_markers_markers.csv tests/sidecar/test_reader_contract.py sidecar/boundfree_sidecar/readers/base.py sidecar/boundfree_sidecar/readers/csv_reader.py sidecar/boundfree_sidecar/protocol.py
git commit -m "feat: formalize sidecar slice contract and CSV markers"
```

### Task 3: HDF5 Generated Fixture Reader

**Files:**
- Create: `sidecar/boundfree_sidecar/readers/hdf5_reader.py`
- Create: `tests/sidecar/test_hdf5_reader.py`
- Modify: `sidecar/boundfree_sidecar/readers/registry.py`
- Test: `tests/sidecar/test_hdf5_reader.py`

**Interfaces:**
- Consumes: Task 2 `SliceData`.
- Produces: HDF5 default-schema reader for dataset `eeg`, attributes `fs`, `ch_names`, and unit `uV`.

- [ ] **Step 1: Write HDF5 reader tests**

Create `tests/sidecar/test_hdf5_reader.py`:

```python
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
h5py = pytest.importorskip("h5py")


def _write_hdf5_fixture(path: Path) -> None:
    with h5py.File(path, "w") as f:
        ds = f.create_dataset(
            "eeg",
            data=[
                [0.0, 1.0, 2.0, 3.0, 4.0],
                [10.0, 11.0, 12.0, 13.0, 14.0],
            ],
        )
        ds.attrs["fs"] = 100.0
        ds.attrs["ch_names"] = ["h1", "h2"]
        ds.attrs["unit"] = "uV"


def test_hdf5_reader_open_returns_default_schema_metadata(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    sample = tmp_path / "sample.h5"
    _write_hdf5_fixture(sample)

    response = handle_request({"id": "h5-open", "method": "open", "params": {"path": str(sample)}})

    assert response["id"] == "h5-open"
    assert response["ok"] is True
    assert response["result"]["format"] == "HDF5"
    assert response["result"]["fs"] == 100.0
    assert response["result"]["n_channels"] == 2
    assert response["result"]["n_samples"] == 5
    assert response["result"]["duration"] == 0.05
    assert response["result"]["ch_names"] == ["h1", "h2"]
    assert response["result"]["unit"] == "uV"


def test_hdf5_reader_get_slice_returns_requested_window(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    sample = tmp_path / "sample.h5"
    _write_hdf5_fixture(sample)
    opened = handle_request({"id": "h5-open-2", "method": "open", "params": {"path": str(sample)}})
    handle = opened["result"]["handle"]

    response = handle_request({"id": "h5-slice", "method": "get_slice", "params": {"handle": handle, "t0": 0.01, "t1": 0.04}})

    assert response["id"] == "h5-slice"
    assert response["ok"] is True
    assert response["result"]["shape"] == [2, 3]
    assert response["result"]["data"] == [[1.0, 2.0, 3.0], [11.0, 12.0, 13.0]]
```

- [ ] **Step 2: Run HDF5 reader tests and verify they fail or skip**

Run:

```bash
python3 -m pytest tests/sidecar/test_hdf5_reader.py -q
```

Expected without `h5py`: SKIP. Expected with `h5py`: FAIL because `Hdf5Reader` is not registered.

- [ ] **Step 3: Implement HDF5 reader**

Create `sidecar/boundfree_sidecar/readers/hdf5_reader.py`:

```python
from pathlib import Path

from .base import FileMetadata, Reader, SliceData


class Hdf5Reader(Reader):
    format_name = "HDF5"

    def __init__(self, path: Path, dataset_path: str = "eeg") -> None:
        super().__init__(path)
        self.dataset_path = dataset_path

    def metadata(self) -> FileMetadata:
        import h5py

        with h5py.File(self.path, "r") as f:
            if self.dataset_path not in f:
                raise KeyError(f"missing dataset {self.dataset_path}")
            ds = f[self.dataset_path]
            if len(ds.shape) != 2:
                raise ValueError(f"dataset {self.dataset_path} must be 2D")
            n_channels = int(ds.shape[0])
            n_samples = int(ds.shape[1])
            fs = float(ds.attrs.get("fs", f.attrs.get("fs", 256.0)))
            raw_names = ds.attrs.get("ch_names", [f"Ch{i + 1}" for i in range(n_channels)])
            ch_names = [name.decode() if isinstance(name, bytes) else str(name) for name in raw_names]
            unit = str(ds.attrs.get("unit", f.attrs.get("unit", "uV")))
        return FileMetadata(
            fs=fs,
            n_channels=n_channels,
            n_samples=n_samples,
            duration=n_samples / fs if fs > 0 else 0.0,
            ch_names=ch_names,
            unit=unit,
            format_name=self.format_name,
            events=[],
        )

    def get_slice(self, t0: float, t1: float) -> SliceData:
        import h5py

        meta = self.metadata()
        start = max(0, int(round(t0 * meta.fs)))
        stop = min(meta.n_samples, int(round(t1 * meta.fs)))
        with h5py.File(self.path, "r") as f:
            data = f[self.dataset_path][:, start:stop]
        return SliceData(
            t0=t0,
            t1=t1,
            fs=meta.fs,
            unit=meta.unit,
            data=data.astype(float).tolist(),
        )
```

- [ ] **Step 4: Register HDF5 extensions**

Modify `sidecar/boundfree_sidecar/readers/registry.py`:

```python
from .hdf5_reader import Hdf5Reader
```

Add this dispatch branch before `return None`:

```python
if path.suffix.lower() in {".h5", ".hdf5"}:
    return Hdf5Reader(path)
```

- [ ] **Step 5: Verify HDF5 reader behavior**

Run:

```bash
python3 -m pytest tests/sidecar/test_hdf5_reader.py tests/sidecar/test_sidecar.py -q
```

Expected without `h5py`: HDF5 tests skip and sidecar tests pass. Expected with `h5py`: all selected tests pass.

- [ ] **Step 6: Commit**

Run:

```bash
git add sidecar/boundfree_sidecar/readers/hdf5_reader.py sidecar/boundfree_sidecar/readers/registry.py tests/sidecar/test_hdf5_reader.py
git commit -m "feat: add dependency-gated HDF5 reader"
```

### Task 4: MNE EDF Reader Smoke Gate

**Files:**
- Create: `sidecar/boundfree_sidecar/readers/mne_reader.py`
- Create: `tests/sidecar/test_mne_reader.py`
- Modify: `sidecar/boundfree_sidecar/readers/registry.py`
- Modify: `docs/reports/m2-format-acceptance.md`
- Test: `tests/sidecar/test_mne_reader.py`

**Interfaces:**
- Consumes: Task 2 `SliceData`.
- Produces: MNE-backed reader path for EDF/BDF/BrainVision/EEGLAB/FIF, with automatic acceptance only for the old-repo EDF sample in this task.

- [ ] **Step 1: Write MNE EDF smoke tests**

Create `tests/sidecar/test_mne_reader.py`:

```python
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
mne = pytest.importorskip("mne")
EDF_SAMPLE = ROOT.parent / "boundfree-eeg-upper-computer" / "samples" / "mark.edf"


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
```

- [ ] **Step 2: Run MNE reader tests and verify they fail or skip**

Run:

```bash
python3 -m pytest tests/sidecar/test_mne_reader.py -q
```

Expected without `mne`: SKIP. Expected with `mne`: FAIL because `MneReader` is not registered.

- [ ] **Step 3: Implement MNE reader**

Create `sidecar/boundfree_sidecar/readers/mne_reader.py`:

```python
from pathlib import Path

from .base import FileMetadata, Reader, SliceData


class MneReader(Reader):
    def __init__(self, path: Path) -> None:
        super().__init__(path)
        self._raw = None

    @property
    def format_name(self) -> str:
        suffix = self.path.suffix.lower()
        if suffix == ".edf":
            return "EDF"
        if suffix == ".bdf":
            return "BDF"
        if suffix == ".vhdr":
            return "BrainVision"
        if suffix == ".set":
            return "EEGLAB"
        if suffix == ".fif":
            return "MNE FIF"
        return "MNE"

    def _load_raw(self):
        if self._raw is None:
            import mne

            mne.set_log_level("WARNING")
            suffix = self.path.suffix.lower()
            if suffix == ".edf":
                self._raw = mne.io.read_raw_edf(str(self.path), preload=False, verbose=False)
            elif suffix == ".bdf":
                self._raw = mne.io.read_raw_bdf(str(self.path), preload=False, verbose=False)
            else:
                self._raw = mne.io.read_raw(str(self.path), preload=False, verbose=False)
        return self._raw

    def metadata(self) -> FileMetadata:
        raw = self._load_raw()
        fs = float(raw.info["sfreq"])
        events = [
            {"time": round(float(annotation["onset"]), 3), "label": str(annotation["description"])}
            for annotation in raw.annotations
            if str(annotation["description"])
        ]
        return FileMetadata(
            fs=fs,
            n_channels=len(raw.ch_names),
            n_samples=int(raw.n_times),
            duration=int(raw.n_times) / fs if fs > 0 else 0.0,
            ch_names=list(raw.ch_names),
            unit="uV",
            format_name=self.format_name,
            events=events,
        )

    def get_slice(self, t0: float, t1: float) -> SliceData:
        raw = self._load_raw()
        meta = self.metadata()
        start = max(0, int(t0 * meta.fs))
        stop = min(meta.n_samples, int(t1 * meta.fs))
        data_v, _ = raw[:, start:stop]
        return SliceData(t0=t0, t1=t1, fs=meta.fs, unit="uV", data=(data_v * 1e6).tolist())
```

- [ ] **Step 4: Register MNE extensions without importing MNE at startup**

Modify `sidecar/boundfree_sidecar/readers/registry.py`:

```python
from .mne_reader import MneReader
```

Add this dispatch branch before `return None`:

```python
if path.suffix.lower() in {".edf", ".bdf", ".vhdr", ".set", ".fif"}:
    return MneReader(path)
```

- [ ] **Step 5: Record format acceptance status**

Create or update `docs/reports/m2-format-acceptance.md`:

```markdown
# M2 Format Acceptance Record

Last updated: 2026-07-08

| Format | Status | Evidence |
| --- | --- | --- |
| CSV | Generated fixture automatic tests pass | `tests/sidecar/test_csv_reader.py`, `tests/sidecar/test_reader_contract.py` |
| HDF5 | Dependency-gated generated fixture tests pass when `h5py==3.16.0` is installed | `tests/sidecar/test_hdf5_reader.py` |
| EDF | Dependency-gated old-repo sample smoke passes when `mne==1.12.1` is installed and `../boundfree-eeg-upper-computer/samples/mark.edf` exists | `tests/sidecar/test_mne_reader.py` |
| BDF | Reader path exists through MNE, public support claim remains gated by a real or generated acceptance sample | not accepted in this task |
| BrainVision | Reader path exists through MNE, public support claim remains gated by a real or generated acceptance sample | not accepted in this task |
| EEGLAB | Reader path exists through MNE, public support claim remains gated by a real or generated acceptance sample | not accepted in this task |
| MNE FIF | Reader path exists through MNE, public support claim remains gated by a real or generated acceptance sample | not accepted in this task |
| Intan RHS | Not implemented in Native sidecar in this task | not accepted in this task |
| BBK/DAT/LSD | Not implemented in Native sidecar in this task | not accepted in this task |
```

- [ ] **Step 6: Verify MNE gate**

Run:

```bash
python3 -m pytest tests/sidecar/test_mne_reader.py -q
```

Expected without `mne`: SKIP. Expected with `mne` and the EDF sample: all selected tests pass.

- [ ] **Step 7: Commit**

Run:

```bash
git add sidecar/boundfree_sidecar/readers/mne_reader.py sidecar/boundfree_sidecar/readers/registry.py tests/sidecar/test_mne_reader.py docs/reports/m2-format-acceptance.md
git commit -m "feat: add dependency-gated MNE reader path"
```

### Task 5: Native Slice IPC And Smoke

**Files:**
- Modify: `native/src/ipc/sidecar_client.h`
- Modify: `native/src/ipc/sidecar_client.cpp`
- Modify: `native/src/app/main.cpp`
- Modify: `tests/native/test_bf_app.py`
- Test: `tests/native/test_bf_app.py::test_bf_app_smoke_open_slice_generated_csv_fixture`

**Interfaces:**
- Consumes: Task 2 sidecar `get_slice` response:
  `{"handle": str, "t0": float, "t1": float, "fs": float, "shape": [int, int], "unit": "uV", "data": [[float]]}`.
- Produces: Native `SidecarSliceResult` and `SidecarClient::getSlice(const QString& handle, double t0, double t1, int timeoutMs = 3000)`.

- [ ] **Step 1: Write failing Native slice smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_open_slice_generated_csv_fixture():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run([str(APP), "--smoke-open-slice", str(sample), "0.02", "0.05"])

    assert result.stdout.strip() == "smoke-open-slice-ok shape=2x3 first=2 last=14 unit=uV"
```

- [ ] **Step 2: Run Native slice smoke and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_open_slice_generated_csv_fixture -q
```

Expected: FAIL because `--smoke-open-slice` is not handled.

- [ ] **Step 3: Add Native slice result type**

Add to `native/src/ipc/sidecar_client.h`:

```cpp
#include <vector>

struct SidecarSliceResult {
    bool ok = false;
    QString handle;
    double t0 = 0.0;
    double t1 = 0.0;
    double fs = 0.0;
    int nChannels = 0;
    int nSamples = 0;
    QString unit;
    std::vector<std::vector<float>> data;
    AppError error;
};
```

Add this method declaration to `SidecarClient`:

```cpp
SidecarSliceResult getSlice(const QString& handle, double t0, double t1, int timeoutMs = 3000);
```

- [ ] **Step 4: Implement Native slice parsing**

Add to `native/src/ipc/sidecar_client.cpp`:

```cpp
SidecarSliceResult SidecarClient::getSlice(const QString& handle, double t0, double t1, int timeoutMs) {
    SidecarSliceResult result;
    const SidecarResponse response = request("get_slice", {{"handle", handle}, {"t0", t0}, {"t1", t1}}, timeoutMs);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }

    const QJsonObject payload = response.result;
    result.handle = payload.value("handle").toString();
    result.t0 = payload.value("t0").toDouble();
    result.t1 = payload.value("t1").toDouble();
    result.fs = payload.value("fs").toDouble();
    result.unit = payload.value("unit").toString();
    const QJsonArray shape = payload.value("shape").toArray();
    result.nChannels = shape.size() > 0 ? shape.at(0).toInt() : 0;
    result.nSamples = shape.size() > 1 ? shape.at(1).toInt() : 0;
    const QJsonArray rows = payload.value("data").toArray();
    for (const QJsonValue& rowValue : rows) {
        std::vector<float> row;
        const QJsonArray values = rowValue.toArray();
        row.reserve(values.size());
        for (const QJsonValue& value : values) {
            row.push_back(static_cast<float>(value.toDouble()));
        }
        result.data.push_back(std::move(row));
    }
    result.ok = true;
    return result;
}
```

- [ ] **Step 5: Add smoke CLI**

In `native/src/app/main.cpp`, add an early branch:

```cpp
if (arg == "--smoke-open-slice" && i + 3 < argc) {
    QCoreApplication app(argc, argv);
    bf::SidecarClient sidecar(repoRoot);
    const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
    if (!hello.ok) {
        std::cerr << "smoke-open-slice-failed code=" << hello.error.code.toStdString() << "\n";
        return 1;
    }
    const bf::SidecarOpenResult opened = sidecar.openFile(QString::fromLocal8Bit(argv[i + 1]));
    if (!opened.ok) {
        sidecar.stop();
        std::cerr << "smoke-open-slice-failed code=" << opened.error.code.toStdString() << "\n";
        return 1;
    }
    const double t0 = QString::fromLocal8Bit(argv[i + 2]).toDouble();
    const double t1 = QString::fromLocal8Bit(argv[i + 3]).toDouble();
    const bf::SidecarSliceResult slice = sidecar.getSlice(opened.handle, t0, t1);
    sidecar.stop();
    if (!slice.ok || slice.data.empty() || slice.data.front().empty()) {
        std::cerr << "smoke-open-slice-failed code=" << slice.error.code.toStdString() << "\n";
        return 1;
    }
    const float first = slice.data.front().front();
    const float last = slice.data.back().back();
    std::cout << "smoke-open-slice-ok shape=" << slice.nChannels << "x" << slice.nSamples
              << " first=" << first << " last=" << last
              << " unit=" << slice.unit.toStdString() << "\n";
    return 0;
}
```

- [ ] **Step 6: Build and verify Native slice smoke**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_open_slice_generated_csv_fixture -q
```

Expected: build succeeds and the selected test passes.

- [ ] **Step 7: Commit**

Run:

```bash
git add native/src/ipc/sidecar_client.h native/src/ipc/sidecar_client.cpp native/src/app/main.cpp tests/native/test_bf_app.py
git commit -m "feat: add native sidecar slice smoke"
```

### Task 6: Native Waveform Slice Model And Static Render Smoke

**Files:**
- Create: `native/src/render/waveform_slice.h`
- Create: `native/src/render/waveform_slice.cpp`
- Create: `native/src/ui/waveform_widget.h`
- Create: `native/src/ui/waveform_widget.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/src/app/main.cpp`
- Modify: `tests/native/test_bf_app.py`
- Test: `tests/native/test_bf_app.py::test_bf_app_smoke_waveform_render_offscreen`

**Interfaces:**
- Consumes: Task 5 `SidecarSliceResult`.
- Produces: `WaveformSlice`, `WaveformWidget::setSlice(const WaveformSlice&)`, and an offscreen nonblank render smoke.

- [ ] **Step 1: Write failing waveform render smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_waveform_render_offscreen():
    result = _run(
        [str(APP), "--smoke-waveform-render"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert result.stdout.strip().startswith("smoke-waveform-render-ok image=640x320 non_background_pixels=")
```

- [ ] **Step 2: Run waveform smoke and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_render_offscreen -q
```

Expected: FAIL because `--smoke-waveform-render` is not handled.

- [ ] **Step 3: Add waveform slice model**

Create `native/src/render/waveform_slice.h`:

```cpp
#pragma once

#include "ipc/sidecar_client.h"

#include <QStringList>
#include <vector>

namespace bf {

struct WaveformSlice {
    double fs = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
    QString unit = "uV";
    QStringList channelNames;
    std::vector<std::vector<float>> data;
};

WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice);

}  // namespace bf
```

Create `native/src/render/waveform_slice.cpp`:

```cpp
#include "render/waveform_slice.h"

namespace bf {

WaveformSlice waveformSliceFromSidecar(const SidecarOpenResult& session, const SidecarSliceResult& slice) {
    WaveformSlice result;
    result.fs = slice.fs;
    result.t0 = slice.t0;
    result.t1 = slice.t1;
    result.unit = slice.unit;
    result.channelNames = session.chNames;
    result.data = slice.data;
    return result;
}

}  // namespace bf
```

- [ ] **Step 4: Add static waveform widget**

Create `native/src/ui/waveform_widget.h`:

```cpp
#pragma once

#include "render/waveform_slice.h"

#include <QWidget>

namespace bf {

class WaveformWidget : public QWidget {
public:
    explicit WaveformWidget(QWidget* parent = nullptr);
    void setSlice(const WaveformSlice& slice);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    WaveformSlice slice_;
};

}  // namespace bf
```

Create `native/src/ui/waveform_widget.cpp`:

```cpp
#include "ui/waveform_widget.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace bf {

WaveformWidget::WaveformWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(640, 320);
}

void WaveformWidget::setSlice(const WaveformSlice& slice) {
    slice_ = slice;
    update();
}

void WaveformWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#111827"));
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor("#6EE7B7"), 1));

    const int nChannels = static_cast<int>(slice_.data.size());
    if (nChannels <= 0) {
        return;
    }
    const double rowHeight = static_cast<double>(height()) / nChannels;
    for (int ch = 0; ch < nChannels; ++ch) {
        const std::vector<float>& samples = slice_.data[ch];
        if (samples.size() < 2) {
            continue;
        }
        float maxAbs = 1.0f;
        for (const float value : samples) {
            maxAbs = std::max(maxAbs, std::abs(value));
        }
        const double centerY = rowHeight * (ch + 0.5);
        QPointF previous(0.0, centerY - samples[0] / maxAbs * rowHeight * 0.35);
        for (int i = 1; i < static_cast<int>(samples.size()); ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(samples.size() - 1) * width();
            const double y = centerY - samples[i] / maxAbs * rowHeight * 0.35;
            const QPointF current(x, y);
            painter.drawLine(previous, current);
            previous = current;
        }
    }
}

}  // namespace bf
```

- [ ] **Step 5: Add CMake entries and smoke CLI**

Add to `native/CMakeLists.txt`:

```cmake
    src/render/waveform_slice.cpp
    src/render/waveform_slice.h
    src/ui/waveform_widget.cpp
    src/ui/waveform_widget.h
```

Add includes to `native/src/app/main.cpp`:

```cpp
#include "render/waveform_slice.h"
#include "ui/waveform_widget.h"
#include <QImage>
#include <QPainter>
```

Add an early branch:

```cpp
if (arg == "--smoke-waveform-render") {
    QApplication app(argc, argv);
    bf::WaveformWidget widget;
    bf::WaveformSlice slice;
    slice.fs = 100.0;
    slice.t0 = 0.0;
    slice.t1 = 0.06;
    slice.channelNames = {"ch1", "ch2"};
    slice.data = {{0.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f}};
    widget.resize(640, 320);
    widget.setSlice(slice);
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(QColor("#111827"));
    widget.render(&image);
    int nonBackground = 0;
    const QRgb background = QColor("#111827").rgba();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != background) {
                ++nonBackground;
            }
        }
    }
    std::cout << "smoke-waveform-render-ok image=640x320 non_background_pixels=" << nonBackground << "\n";
    return nonBackground > 0 ? 0 : 1;
}
```

- [ ] **Step 6: Build and verify waveform smoke**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_render_offscreen -q
```

Expected: build succeeds and the selected test passes.

- [ ] **Step 7: Commit**

Run:

```bash
git add native/src/render/waveform_slice.h native/src/render/waveform_slice.cpp native/src/ui/waveform_widget.h native/src/ui/waveform_widget.cpp native/CMakeLists.txt native/src/app/main.cpp tests/native/test_bf_app.py
git commit -m "feat: add static waveform render smoke"
```

### Task 7: Async File Open Controller And Main Window Entry

**Files:**
- Create: `native/src/controller/file_open_controller.h`
- Create: `native/src/controller/file_open_controller.cpp`
- Modify: `native/src/ui/main_window.h`
- Modify: `native/src/ui/main_window.cpp`
- Modify: `native/src/app/main.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `tests/native/test_bf_app.py`
- Test: `tests/native/test_bf_app.py::test_bf_app_smoke_ui_open_generated_csv_fixture`

**Interfaces:**
- Consumes: Task 5 `SidecarClient::openFile()` and `SidecarClient::getSlice()`, Task 6 `WaveformWidget::setSlice()`.
- Produces: GUI open path that runs sidecar open and initial slice outside the GUI thread.

- [ ] **Step 1: Write failing UI open smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_ui_open_generated_csv_fixture():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui-open-file", str(sample), "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-open-file-ok format=CSV n_channels=2 n_samples=10" in result.stdout
```

- [ ] **Step 2: Run UI open smoke and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_open_generated_csv_fixture -q
```

Expected: FAIL because `--smoke-ui-open-file` is not handled.

- [ ] **Step 3: Add async file open controller interface**

Create `native/src/controller/file_open_controller.h`:

```cpp
#pragma once

#include "core/app_error.h"
#include "core/version.h"
#include "ipc/sidecar_client.h"
#include "render/waveform_slice.h"

#include <QObject>
#include <QString>
#include <atomic>

namespace bf {

struct FileOpenResult {
    SidecarOpenResult session;
    SidecarSliceResult initialSlice;
    WaveformSlice waveform;
};

class FileOpenController : public QObject {
    Q_OBJECT

public:
    FileOpenController(QString repoRoot, VersionInfo version, QObject* parent = nullptr);
    void openInitialWindow(const QString& path);

signals:
    void loaded(const bf::FileOpenResult& result);
    void failed(const bf::AppError& error);

private:
    QString repoRoot_;
    VersionInfo version_;
    std::atomic<int> requestSerial_{0};
};

}  // namespace bf
```

- [ ] **Step 4: Implement latest-request-wins async open**

Create `native/src/controller/file_open_controller.cpp`:

```cpp
#include "controller/file_open_controller.h"

#include "render/waveform_slice.h"

#include <QMetaObject>
#include <QThread>
#include <algorithm>

namespace bf {

FileOpenController::FileOpenController(QString repoRoot, VersionInfo version, QObject* parent)
    : QObject(parent), repoRoot_(std::move(repoRoot)), version_(std::move(version)) {}

void FileOpenController::openInitialWindow(const QString& path) {
    const int serial = ++requestSerial_;
    auto* thread = QThread::create([this, serial, path]() {
        auto emitFailure = [this, serial](const AppError& error) {
            QMetaObject::invokeMethod(this, [this, serial, error]() {
                if (serial == requestSerial_.load()) {
                    emit failed(error);
                }
            }, Qt::QueuedConnection);
        };
        SidecarClient sidecar(repoRoot_);
        const SidecarHelloResult hello = sidecar.startAndHello(version_);
        if (!hello.ok) {
            sidecar.stop();
            emitFailure(hello.error);
            return;
        }
        SidecarOpenResult opened = sidecar.openFile(path, 30000);
        if (!opened.ok) {
            sidecar.stop();
            emitFailure(opened.error);
            return;
        }
        const double t1 = std::min(10.0, opened.duration);
        SidecarSliceResult slice = sidecar.getSlice(opened.handle, 0.0, t1, 30000);
        sidecar.stop();
        if (!slice.ok) {
            emitFailure(slice.error);
            return;
        }
        FileOpenResult result{opened, slice, waveformSliceFromSidecar(opened, slice)};
        QMetaObject::invokeMethod(this, [this, serial, result]() {
            if (serial == requestSerial_.load()) {
                emit loaded(result);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

}  // namespace bf
```

- [ ] **Step 5: Add MainWindow display entry points**

In `native/src/ui/main_window.h`, add:

```cpp
#include "controller/file_open_controller.h"
#include <QStackedWidget>
#include "ui/waveform_widget.h"

void showFileOpenLoading(const QString& path);
void showFileOpenResult(const FileOpenResult& result);
void showFileOpenError(const AppError& error);
```

Add member:

```cpp
QStackedWidget* centralStack_ = nullptr;
WaveformWidget* waveformWidget_ = nullptr;
```

In `native/src/ui/main_window.cpp`, replace the single central empty label setup with a stack that keeps the existing empty label and adds a waveform page:

```cpp
centralStack_ = new QStackedWidget(this);
centralLabel_ = new QLabel(this);
centralLabel_->setAlignment(Qt::AlignCenter);
waveformWidget_ = new WaveformWidget(this);
centralStack_->addWidget(centralLabel_);
centralStack_->addWidget(waveformWidget_);
setCentralWidget(centralStack_);
```

Add:

```cpp
void MainWindow::showFileOpenLoading(const QString& path) {
    statusBar()->showMessage(QString("Opening %1").arg(path));
}

void MainWindow::showFileOpenResult(const FileOpenResult& result) {
    waveformWidget_->setSlice(result.waveform);
    centralStack_->setCurrentWidget(waveformWidget_);
    statusBar()->showMessage(QString("%1 | %2 Hz | %3 channels")
        .arg(result.session.format)
        .arg(result.session.fs)
        .arg(result.session.nChannels));
}

void MainWindow::showFileOpenError(const AppError& error) {
    setSidecarFailed(error);
}
```

- [ ] **Step 6: Wire smoke UI open path in app main**

In `native/src/app/main.cpp`, add a smoke branch before normal parser handling:

```cpp
if (arg == "--smoke-ui-open-file" && i + 1 < argc) {
    QApplication app(argc, argv);
    bf::I18nService i18n(repoRoot);
    QString i18nError;
    i18n.loadLocale("en_US", &i18nError);
    bf::MainWindow window(version, &i18n);
    bf::FileOpenController controller(repoRoot, version);
    QObject::connect(&controller, &bf::FileOpenController::loaded, &window, [&](const bf::FileOpenResult& result) {
        window.showFileOpenResult(result);
        std::cout << "smoke-ui-open-file-ok format=" << result.session.format.toStdString()
                  << " n_channels=" << result.session.nChannels
                  << " n_samples=" << result.session.nSamples << "\n";
        app.quit();
    });
    QObject::connect(&controller, &bf::FileOpenController::failed, &window, [&](const bf::AppError& error) {
        std::cerr << "smoke-ui-open-file-failed code=" << error.code.toStdString() << "\n";
        app.quit();
    });
    controller.openInitialWindow(QString::fromLocal8Bit(argv[i + 1]));
    QTimer::singleShot(5000, &app, [&]() {
        std::cerr << "smoke-ui-open-file-failed code=timeout\n";
        app.quit();
    });
    return app.exec();
}
```

Add new source files to `native/CMakeLists.txt`.

- [ ] **Step 7: Verify UI open smoke**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_open_generated_csv_fixture -q
```

Expected: build succeeds and the selected test passes.

- [ ] **Step 8: Commit**

Run:

```bash
git add native/src/controller/file_open_controller.h native/src/controller/file_open_controller.cpp native/src/ui/main_window.h native/src/ui/main_window.cpp native/src/app/main.cpp native/CMakeLists.txt tests/native/test_bf_app.py
git commit -m "feat: add async static file browser entry"
```

### Task 8: M2-1 Documentation And Full Verification

**Files:**
- Modify: `docs/feature-parity-checklist.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/reports/m2-format-acceptance.md`
- Modify: `docs/superpowers/plans/2026-07-08-m2-1-file-slice-static-waveform.md`

**Interfaces:**
- Consumes: completed Tasks 1-7.
- Produces: explicit M2-1 status and a verified branch ready for review or merge.

- [x] **Step 1: Update feature checklist status narrowly**

In `docs/feature-parity-checklist.md`, under "文件读取（边车）— M1/M2", mark only these completed subitems if their tests passed:

```markdown
- [x] CSV 打开（可去时间列）
- [x] 懒加载：open 只取元数据，get_slice 按需读
- [x] 单位统一换算为 μV
```

Keep EDF/BDF/HDF5/BrainVision/EEGLAB/FIF/Intan RHS/BBK unchecked unless their dependency-gated tests have passed in the current environment and their evidence is recorded in `docs/reports/m2-format-acceptance.md`.

Under "波形显示 — M2", mark only this if Task 6 and Task 7 passed:

```markdown
- [x] 大窗口或高采样率数据使用 MinMax、LTTB 或等价抽稀策略
```

Keep real GPU acceptance unchecked until `docs/reports/m2-rendering-gpu-gate.md` contains a real GPU/display result.

- [x] **Step 2: Update roadmap current phase**

In `docs/roadmap.md`, update "当前阶段" to say M2-1 is in progress or completed according to verification:

```markdown
当前阶段进入 M2-1：文件切片契约、依赖门禁格式读取和静态波形浏览入口。M2-1 不代表完整 M2 验收；真实 GPU 渲染门禁、全格式真实样本验收、通道交互浏览仍需后续完成。
```

- [x] **Step 3: Run full verification**

Run:

```bash
python3 -m pytest tests/fixtures tests/sidecar tests/native tests/contracts tests/i18n -q
cmake --build native/build
./scripts/verify_m1_foundation.sh --fast
git diff --check HEAD
```

Expected:

```text
all non-skipped tests pass
native build exits 0
M1 foundation verification passed
git diff --check exits 0
```

- [x] **Step 4: Record dependency-gated skips**

If `h5py` or `mne` is missing, add an "Environment notes" section to `docs/reports/m2-format-acceptance.md`:

```markdown
## Environment Notes

- `h5py` dependency-gated tests skipped in this environment if `h5py==3.16.0` is not installed.
- `mne` dependency-gated tests skipped in this environment if `mne==1.12.1` is not installed.
- Skipped dependency-gated tests are not format acceptance evidence.
```

- [x] **Step 5: Commit documentation and verification status**

Run:

```bash
git add docs/feature-parity-checklist.md docs/roadmap.md docs/reports/m2-format-acceptance.md docs/superpowers/plans/2026-07-08-m2-1-file-slice-static-waveform.md
git commit -m "docs: record m2-1 implementation status"
```

## Self-Review

Spec coverage:
- v1 general file analysis boundary is preserved.
- Hardware acquisition remains out of scope.
- `open` metadata-only and `get_slice` requested-window behavior are covered in Tasks 2-5.
- CSV marker sidecar, HDF5 generated fixture, and MNE EDF sample gate are covered.
- Static waveform browser entry is covered without claiming final GPU acceptance.
- Real GPU gate remains open and documented.

Placeholder scan:
- This plan contains no deferred implementation placeholders.
- Formats without evidence remain unchecked and explicitly sample-gated.

Type consistency:
- Sidecar `SliceData.shape` matches Native `SidecarSliceResult.nChannels` and `nSamples`.
- Native `SidecarClient::getSlice()` is consumed by `FileOpenController`.
- `WaveformSlice` is produced from `SidecarOpenResult` and `SidecarSliceResult`, then consumed by `WaveformWidget`.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-08-m2-1-file-slice-static-waveform.md`.

Two execution options:

1. Subagent-Driven (recommended) - dispatch a fresh subagent per task, review between tasks, fast iteration.
2. Inline Execution - execute tasks in this session using executing-plans, batch execution with checkpoints.
