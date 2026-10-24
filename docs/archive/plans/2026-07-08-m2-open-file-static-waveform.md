---
title: M2 文件打开与静态波形入口实施计划
doc_type: plan
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-07-08
archived_at: 2026-10-23
---

# M2 Open File Static Waveform Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first testable v1 file-analysis path: open a generated EEG fixture through the Python sidecar, lazy-load metadata, fetch a visible time-window slice, and prepare static waveform rendering behind a real GPU acceptance gate.

**Architecture:** Python sidecar owns file readers and slice extraction. C++ owns session state, latest-request-wins coordination, decimation, and UI presentation; M2 starts with a generated CSV fixture before expanding through the v1 required format matrix.

**Tech Stack:** C++17, Qt6 Core/Widgets/OpenGL, Python 3, pytest, CSV fixture reader, JSON IPC for metadata, little-endian `float32` slice payload semantics, MinMax decimation.

## Global Constraints

- Work in `boundfree-community`; use `../boundfree-eeg-upper-computer` only as a reference and golden-file source.
- v1 required parity formats: EDF/EDF+, BDF, HDF5, CSV, BrainVision, EEGLAB, MNE FIF, Intan RHS, BBK/DAT/LSD.
- v1 target formats with real-sample gate: Intan RHD/RHS, Open Ephys, Plexon, Blackrock.
- No format can be announced as supported without real samples and an automatic or manual验收记录.
- `open` reads metadata only; large files must not be fully loaded during open.
- `get_slice` reads the requested time interval only.
- EEG data shape is `[n_channels, n_samples]`, unit `uV`.
- GUI main thread must not do file IO, DSP, export, or long-running analysis.
- Fast seek and quick file switching use latest-request-wins; older results must not overwrite newer windows.
- WSL/Xvfb/Mesa rendering results are development signals only; real GPU/display verification is required before M2 waveform acceptance.

---

### Task 1: Sample-Gated Format Matrix

**Files:**
- Modify: `docs/feature-parity-checklist.md`
- Create: `tests/fixtures/README.md`
- Create: `tests/fixtures/sample_manifest.json`

**Interfaces:**
- Consumes: v1 file format boundary from `../BoundFree_EEG_System_Requirements_Spec.md`.
- Produces: a machine-readable fixture manifest that separates required parity formats from sample-gated target formats.

- [x] **Step 1: Create fixture manifest**

Create `tests/fixtures/sample_manifest.json`:

```json
{
  "schema_version": 1,
  "v1_required_parity": [
    {"format": "EDF/EDF+", "extensions": [".edf"], "sample_status": "required_before_v1_release"},
    {"format": "BDF", "extensions": [".bdf"], "sample_status": "required_before_v1_release"},
    {"format": "HDF5", "extensions": [".h5", ".hdf5"], "sample_status": "required_before_v1_release"},
    {"format": "CSV", "extensions": [".csv"], "sample_status": "generated_fixture_available"},
    {"format": "BrainVision", "extensions": [".vhdr"], "sample_status": "required_before_v1_release"},
    {"format": "EEGLAB", "extensions": [".set"], "sample_status": "required_before_v1_release"},
    {"format": "MNE FIF", "extensions": [".fif"], "sample_status": "required_before_v1_release"},
    {"format": "Intan RHS", "extensions": [".rhs"], "sample_status": "required_before_v1_release"},
    {"format": "BBK/DAT/LSD", "extensions": [".dat", ".bbk", ".lsd"], "sample_status": "required_before_v1_release"}
  ],
  "v1_target_real_sample_gate": [
    {"format": "Intan RHD/RHS", "extensions": [".rhd", ".rhs"], "minimum_claim": "continuous_signal_metadata_ttl_if_sampled"},
    {"format": "Open Ephys", "extensions": [".oebin", ".dat", ".nwb"], "minimum_claim": "continuous_signal_metadata_ttl_if_sampled"},
    {"format": "Plexon", "extensions": [".plx", ".pl2"], "minimum_claim": "continuous_signal_metadata_events_if_sampled"},
    {"format": "Blackrock", "extensions": [".nsx", ".nev"], "minimum_claim": "continuous_signal_metadata_events_if_sampled"}
  ]
}
```

- [x] **Step 2: Create fixture README**

Create `tests/fixtures/README.md`:

```markdown
# Test Fixtures

This directory stores generated fixtures and real-sample manifest metadata for BoundFree EEG Native.

`sample_manifest.json` separates v1 required parity formats from v1 target formats that need a real-sample gate before public support claims.

Generated CSV fixtures may be committed. Real customer or vendor samples must be stored according to the project data policy and referenced by manifest entry, not copied into the repository without approval.
```

- [x] **Step 3: Add manifest validation test**

Create `tests/fixtures/test_sample_manifest.py`:

```python
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
```

- [x] **Step 4: Run manifest test**

Run:

```bash
python -m pytest tests/fixtures/test_sample_manifest.py -q
```

Expected: PASS.

### Task 2: Generated CSV Reader Path

**Files:**
- Create: `tests/fixtures/generated/simple_eeg.csv`
- Create: `sidecar/boundfree_sidecar/readers/__init__.py`
- Create: `sidecar/boundfree_sidecar/readers/base.py`
- Create: `sidecar/boundfree_sidecar/readers/csv_reader.py`
- Create: `sidecar/boundfree_sidecar/readers/registry.py`
- Modify: `sidecar/boundfree_sidecar/protocol.py`
- Test: `tests/sidecar/test_csv_reader.py`

**Interfaces:**
- Consumes: M1-B `FileSessionRegistry` and `open` / `get_slice` / `close` protocol methods.
- Produces: first successful metadata and slice path for a small generated CSV fixture.

- [x] **Step 1: Create a deterministic CSV fixture**

Create `tests/fixtures/generated/simple_eeg.csv`:

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

- [x] **Step 2: Write failing reader tests**

Create `tests/sidecar/test_csv_reader.py`:

```python
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"


def test_csv_reader_open_returns_metadata_only():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "csv-open", "method": "open", "params": {"path": str(SAMPLE)}})

    assert response["id"] == "csv-open"
    assert response["ok"] is True
    assert response["result"] == {
        "handle": "file-1",
        "fs": 100.0,
        "n_channels": 2,
        "n_samples": 10,
        "duration": 0.1,
        "ch_names": ["ch1", "ch2"],
        "unit": "uV",
        "format": "CSV",
        "events": [],
    }


def test_csv_reader_get_slice_returns_visible_window_data():
    from boundfree_sidecar.protocol import handle_request

    opened = handle_request({"id": "csv-open-2", "method": "open", "params": {"path": str(SAMPLE)}})
    handle = opened["result"]["handle"]

    response = handle_request({"id": "csv-slice", "method": "get_slice", "params": {"handle": handle, "t0": 0.02, "t1": 0.05}})

    assert response["id"] == "csv-slice"
    assert response["ok"] is True
    assert response["result"] == {
        "handle": handle,
        "t0": 0.02,
        "t1": 0.05,
        "fs": 100.0,
        "shape": [2, 3],
        "unit": "uV",
        "data": [[2.0, 3.0, 4.0], [12.0, 13.0, 14.0]],
    }
```

- [x] **Step 3: Run CSV reader tests and verify they fail**

Run:

```bash
python -m pytest tests/sidecar/test_csv_reader.py -q
```

Expected: FAIL because `.csv` still returns `reader.unsupported_format`.

- [x] **Step 4: Create reader interfaces**

Create `sidecar/boundfree_sidecar/readers/base.py`:

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


class Reader:
    format_name = ""

    def __init__(self, path: Path) -> None:
        self.path = path

    def metadata(self) -> FileMetadata:
        raise NotImplementedError

    def get_slice(self, t0: float, t1: float) -> list[list[float]]:
        raise NotImplementedError
```

Create `sidecar/boundfree_sidecar/readers/registry.py`:

```python
from pathlib import Path

from .base import Reader
from .csv_reader import CsvReader


def reader_for_path(path: Path) -> Reader | None:
    if path.suffix.lower() == ".csv":
        return CsvReader(path)
    return None
```

- [x] **Step 5: Implement CSV reader**

Create `sidecar/boundfree_sidecar/readers/csv_reader.py`:

```python
import csv
from pathlib import Path

from .base import FileMetadata, Reader


class CsvReader(Reader):
    format_name = "CSV"

    def __init__(self, path: Path) -> None:
        super().__init__(path)
        self._rows: list[dict[str, str]] | None = None

    def _load_rows(self) -> list[dict[str, str]]:
        if self._rows is None:
            with self.path.open("r", encoding="utf-8", newline="") as f:
                self._rows = list(csv.DictReader(f))
        return self._rows

    def metadata(self) -> FileMetadata:
        rows = self._load_rows()
        ch_names = [name for name in rows[0].keys() if name != "time_s"]
        times = [float(row["time_s"]) for row in rows]
        fs = 1.0 / (times[1] - times[0]) if len(times) > 1 else 1.0
        duration = len(rows) / fs
        return FileMetadata(
            fs=fs,
            n_channels=len(ch_names),
            n_samples=len(rows),
            duration=duration,
            ch_names=ch_names,
            unit="uV",
            format_name=self.format_name,
            events=[],
        )

    def get_slice(self, t0: float, t1: float) -> list[list[float]]:
        rows = self._load_rows()
        meta = self.metadata()
        start = max(0, int(round(t0 * meta.fs)))
        stop = min(meta.n_samples, int(round(t1 * meta.fs)))
        return [
            [float(row[ch]) for row in rows[start:stop]]
            for ch in meta.ch_names
        ]
```

- [x] **Step 6: Store reader in session registry**

Extend `FileSession` in `sidecar/boundfree_sidecar/sessions.py` with a `reader: object` field. Extend `FileSessionRegistry.add(...)` to accept `reader: object` and store it.

In `protocol.py`, replace the unsupported `.csv` branch in `_handle_open`:

```python
reader = reader_for_path(path)
if reader is None:
    return error_response(
        request_id,
        "reader.unsupported_format",
        {"path": raw_path, "suffix": path.suffix.lower()},
    )
meta = reader.metadata()
session = _sessions.add(path, meta.fs, meta.n_channels, meta.n_samples, meta.ch_names, reader)
return success_response(
    request_id,
    {
        "handle": session.handle,
        "fs": meta.fs,
        "n_channels": meta.n_channels,
        "n_samples": meta.n_samples,
        "duration": meta.duration,
        "ch_names": meta.ch_names,
        "unit": meta.unit,
        "format": meta.format_name,
        "events": meta.events,
    },
)
```

In `_handle_get_slice`, replace the unsupported-format result for known sessions:

```python
data = session.reader.get_slice(t0, t1)
return success_response(
    request_id,
    {
        "handle": handle,
        "t0": t0,
        "t1": t1,
        "fs": session.fs,
        "shape": [session.n_channels, len(data[0]) if data else 0],
        "unit": "uV",
        "data": data,
    },
)
```

- [x] **Step 7: Run reader verification**

Run:

```bash
python -m pytest tests/sidecar/test_csv_reader.py tests/sidecar/test_sidecar.py -q
```

Expected: all sidecar reader and protocol tests pass.

### Task 3: Native Session Controller Entry

**Files:**
- Create: `native/src/controller/file_session.h`
- Create: `native/src/controller/file_session.cpp`
- Modify: `native/src/ipc/sidecar_client.h`
- Modify: `native/src/ipc/sidecar_client.cpp`
- Modify: `native/src/app/main.cpp`
- Modify: `native/CMakeLists.txt`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: `SidecarClient::openFile(path)` from M1-B and sidecar CSV metadata from Task 2.
- Produces: C++ `FileSession` value and smoke CLI proving native can open metadata through sidecar.

- [x] **Step 1: Write failing native open smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_open_generated_csv_fixture():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run([str(APP), "--smoke-open-file", str(sample)])

    assert result.stdout.strip() == (
        "smoke-open-file-ok format=CSV fs=100 n_channels=2 "
        "n_samples=10 duration=0.1 channels=ch1,ch2"
    )
```

- [x] **Step 2: Run native open smoke test and verify it fails**

Run:

```bash
python -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_open_generated_csv_fixture -q
```

Expected: FAIL because `--smoke-open-file` is not handled.

- [x] **Step 3: Add file session value type**

Create `native/src/controller/file_session.h`:

```cpp
#pragma once

#include <QString>
#include <QStringList>

namespace bf {

struct FileSession {
    QString handle;
    QString format;
    double fs = 0.0;
    int nChannels = 0;
    int nSamples = 0;
    double duration = 0.0;
    QStringList channelNames;
};

QString formatChannelList(const QStringList& channels);

}  // namespace bf
```

Create `native/src/controller/file_session.cpp`:

```cpp
#include "controller/file_session.h"

namespace bf {

QString formatChannelList(const QStringList& channels) {
    return channels.join(",");
}

}  // namespace bf
```

- [x] **Step 4: Extend `SidecarOpenResult`**

Add fields in `native/src/ipc/sidecar_client.h`:

```cpp
QString format;
int nSamples = 0;
```

Populate them in `SidecarClient::openFile`:

```cpp
result.format = payload.value("format").toString();
result.nSamples = payload.value("n_samples").toInt();
```

- [x] **Step 5: Add smoke CLI**

In `native/src/app/main.cpp`, include:

```cpp
#include "controller/file_session.h"
```

Add an early branch:

```cpp
if (arg == "--smoke-open-file" && i + 1 < argc) {
    QCoreApplication app(argc, argv);
    bf::SidecarClient sidecar(repoRoot);
    const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
    if (!hello.ok) {
        std::cerr << "smoke-open-file-failed code=" << hello.error.code.toStdString() << "\n";
        return 1;
    }
    const bf::SidecarOpenResult opened = sidecar.openFile(QString::fromLocal8Bit(argv[i + 1]));
    sidecar.stop();
    if (!opened.ok) {
        std::cerr << "smoke-open-file-failed code=" << opened.error.code.toStdString() << "\n";
        return 1;
    }
    std::cout << "smoke-open-file-ok format=" << opened.format.toStdString()
              << " fs=" << opened.fs
              << " n_channels=" << opened.nChannels
              << " n_samples=" << opened.nSamples
              << " duration=" << opened.duration
              << " channels=" << bf::formatChannelList(opened.chNames).toStdString() << "\n";
    return 0;
}
```

- [x] **Step 6: Add controller files to CMake and verify**

Add to `native/CMakeLists.txt`:

```cmake
    src/controller/file_session.cpp
    src/controller/file_session.h
```

Run:

```bash
cmake --build native/build
python -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_open_generated_csv_fixture -q
```

Expected: build succeeds; native open smoke test passes.

### Task 4: Slice Model And MinMax Decimation

**Files:**
- Create: `native/src/render/minmax_decimator.h`
- Create: `native/src/render/minmax_decimator.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: `[n_channels, n_samples]` slice arrays once C++ payload decoding is present.
- Produces: deterministic MinMax decimation smoke path for static waveform rendering.

- [x] **Step 1: Write failing decimator smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_minmax_decimator():
    result = _run([str(APP), "--smoke-minmax-decimator"])

    assert result.stdout.strip() == "smoke-minmax-decimator 0:0,1 1:2,3 2:4,5"
```

- [x] **Step 2: Run decimator test and verify it fails**

Run:

```bash
python -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_minmax_decimator -q
```

Expected: FAIL because `--smoke-minmax-decimator` is not handled.

- [x] **Step 3: Add decimator API**

Create `native/src/render/minmax_decimator.h`:

```cpp
#pragma once

#include <QString>
#include <vector>

namespace bf {

struct MinMaxBucket {
    int bucket = 0;
    float min = 0.0f;
    float max = 0.0f;
};

std::vector<MinMaxBucket> minMaxDecimate(const std::vector<float>& samples, int bucketSize);
QString formatMinMaxBuckets(const std::vector<MinMaxBucket>& buckets);

}  // namespace bf
```

Create `native/src/render/minmax_decimator.cpp`:

```cpp
#include "render/minmax_decimator.h"

#include <algorithm>
#include <QStringList>

namespace bf {

std::vector<MinMaxBucket> minMaxDecimate(const std::vector<float>& samples, int bucketSize) {
    std::vector<MinMaxBucket> buckets;
    if (bucketSize <= 0) {
        return buckets;
    }
    for (int start = 0, bucket = 0; start < static_cast<int>(samples.size()); start += bucketSize, ++bucket) {
        const int stop = std::min(start + bucketSize, static_cast<int>(samples.size()));
        auto minmax = std::minmax_element(samples.begin() + start, samples.begin() + stop);
        buckets.push_back(MinMaxBucket{bucket, *minmax.first, *minmax.second});
    }
    return buckets;
}

QString formatMinMaxBuckets(const std::vector<MinMaxBucket>& buckets) {
    QStringList parts;
    for (const MinMaxBucket& bucket : buckets) {
        parts.push_back(QString("%1:%2,%3").arg(bucket.bucket).arg(bucket.min).arg(bucket.max));
    }
    return parts.join(" ");
}

}  // namespace bf
```

- [x] **Step 4: Add smoke CLI and CMake entries**

Add to `native/CMakeLists.txt`:

```cmake
    src/render/minmax_decimator.cpp
    src/render/minmax_decimator.h
```

Include in `native/src/app/main.cpp`:

```cpp
#include "render/minmax_decimator.h"
```

Add early branch:

```cpp
if (arg == "--smoke-minmax-decimator") {
    QCoreApplication app(argc, argv);
    const std::vector<float> samples{0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    std::cout << "smoke-minmax-decimator "
              << bf::formatMinMaxBuckets(bf::minMaxDecimate(samples, 2)).toStdString() << "\n";
    return 0;
}
```

- [x] **Step 5: Run decimator verification**

Run:

```bash
cmake --build native/build
python -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_minmax_decimator -q
```

Expected: build succeeds; decimator smoke test passes.

### Task 5: Real GPU Rendering Gate

**Files:**
- Create: `docs/reports/m2-rendering-gpu-gate.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/feature-parity-checklist.md`

**Interfaces:**
- Consumes: M0 spike renderer and M2 static waveform acceptance rules.
- Produces: explicit gate that prevents WSL/Xvfb/Mesa from being treated as M2 final rendering acceptance.

- [x] **Step 1: Create GPU gate report template**

Create `docs/reports/m2-rendering-gpu-gate.md`:

```markdown
---
title: M2 真实 GPU 渲染门禁记录
doc_type: report
status: active
owner: BoundFree
last_updated: 2026-07-08
---

# M2 真实 GPU 渲染门禁记录

M2 波形浏览验收前必须在真实 Windows 或 Linux GPU/显示环境复测。WSL、Xvfb、Mesa 软件渲染只作为开发信号，不作为最终验收。

## Required Scenario

- Channels: 64, 128, 256.
- Sampling rate: 1 kHz.
- Visible window: 4 s.
- Interactions: scroll, zoom, channel visibility toggle.
- Required record: OS, GPU, driver, Qt version, command, FPS, CPU usage, GPU usage, subjective interaction notes.

## Acceptance

- Preferred: 256 channels at 1 kHz, 4 s visible window, at least 55 FPS.
- Acceptable with note: at least 30 FPS and interactions remain smooth.
- Blocked: below 30 FPS or visible interaction stalls.
```

- [x] **Step 2: Link gate from roadmap and checklist**

In `docs/roadmap.md`, under "M2 前硬门禁", link to `docs/reports/m2-rendering-gpu-gate.md`.

In `docs/feature-parity-checklist.md`, under "波形显示 — M2", add:

```markdown
- [ ] 真实 GPU/显示环境复测记录已写入 `docs/reports/m2-rendering-gpu-gate.md`
```

- [x] **Step 3: Run documentation verification**

Run:

```bash
git diff --check HEAD
```

Expected: no whitespace errors.

### Task 6: M2 Entry Full Verification

**Files:**
- Modify: `docs/superpowers/plans/2026-07-08-m2-open-file-static-waveform.md`

**Interfaces:**
- Consumes: completed Tasks 1-5.
- Produces: verified M2 entry path ready for broader format parity work.

- [x] **Step 1: Run required verification commands**

Run:

```bash
python -m pytest tests/fixtures tests/sidecar tests/native -q
cmake --build native/build
native/build/bf_app --version
```

Expected:

```text
fixture, sidecar, and native tests pass
native build exits 0
BoundFree EEG 0.1.0-dev
```

- [x] **Step 2: Run M1 foundation verification**

Run:

```bash
./scripts/wsl/verify_m1_foundation.sh --fast
```

Expected: `M1 foundation verification passed`.

- [x] **Step 3: Confirm real GPU gate remains open**

Run:

```bash
rg -n "WSL|Xvfb|Mesa|真实 GPU|real GPU" docs/roadmap.md docs/reports/m2-rendering-gpu-gate.md docs/feature-parity-checklist.md
```

Expected: output includes the rule that WSL/Xvfb/Mesa is not final M2 waveform acceptance.
