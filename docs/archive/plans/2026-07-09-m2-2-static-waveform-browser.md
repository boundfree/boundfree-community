---
title: M2-2 Static Waveform Browser Implementation Plan
doc_type: architecture
status: superseded
owner: BoundFree
last_updated: 2026-10-23
archived_at: 2026-10-23
---

# M2-2 Static Waveform Browser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the M2 static waveform browser surface with explicit view state, browser controls, latest-request-wins hardening, render benchmark evidence, and conservative format/status documentation.

**Architecture:** Python sidecar remains the file-format owner for `open` and `get_slice`. Native owns browser view state, request generation, decimation/render presentation, UI controls, and status display. Real GPU/display evidence is recorded separately from offscreen smoke tests and cannot be inferred from WSL/Xvfb/Mesa.

**Tech Stack:** C++17, Qt6 Core/Widgets, Python 3.10+, pytest, optional sidecar dependencies `h5py==3.16.0` and `mne==1.12.1`, QPainter waveform rendering, JSON IPC, existing `bf_app` smoke CLI.

## Global Constraints

- Work in `boundfree-community`; use `../boundfree-eeg-upper-computer` only as a reference implementation, sample source, and golden-file source.
- Do not modify `../boundfree-eeg-upper-computer`.
- v1 stable is a C++/Qt6 Native general EEG/LFP/electrophysiology file analysis product.
- v1 stable does not include self-developed ADS1299/ESP32-C3 BLE acquisition closure; hardware acquisition enters v1.1/v2.
- No direct Native parsing of scientific file formats; file format ownership remains in the Python sidecar.
- Sidecar errors cross IPC as structured error codes and params; Native maps codes through i18n.
- `open` reads metadata only; large files must not be fully loaded during open.
- `get_slice` reads the requested time interval only.
- EEG data shape is `[n_channels, n_samples]`, unit `uV`.
- GUI main thread must not perform file IO, DSP, export, or long-running analysis.
- Fast seek and quick file switching use latest-request-wins; older results must not overwrite newer windows.
- M2-2 does not implement DSP, playback, annotation editing, video sync, segment export, packaging, recorder implementation, BLE device protocol, or self-developed hardware acquisition.
- Real GPU/display verification is required before full M2 waveform acceptance; WSL/Xvfb/Mesa/offscreen render results are development signals only.
- Current environment has `python3`; do not rely on a `python` executable.
- `docs/style-preview.html` is an existing untracked file and is not part of this plan.

---

## File Structure

| Path | Responsibility |
| --- | --- |
| `native/src/controller/request_generation.h` | Small latest-request-wins generation primitive used by async controller code. |
| `native/src/controller/request_generation.cpp` | Request generation implementation and format helper for smoke output. |
| `native/src/controller/waveform_view_state.h` | Browser view state: time window, amplitude scale, scroll, visible channels. |
| `native/src/controller/waveform_view_state.cpp` | Clamp/toggle helpers and stable formatting for smoke tests. |
| `native/src/controller/file_open_controller.h` | Accept explicit window settings and expose current request generation semantics. |
| `native/src/controller/file_open_controller.cpp` | Apply request generation gate and configurable initial slice window. |
| `native/src/render/waveform_slice.h` | Add derived visible/decimated display model types. |
| `native/src/render/waveform_slice.cpp` | Build display model from `WaveformSlice` and `WaveformViewState`. |
| `native/src/ui/waveform_widget.h` | Accept view state and render only visible channels with fixed amplitude scale. |
| `native/src/ui/waveform_widget.cpp` | Draw background, rows, channel labels, and amplitude-scaled traces. |
| `native/src/ui/main_window.h` | Add static browser controls and state. |
| `native/src/ui/main_window.cpp` | Wire toolbar controls, channel visibility, status text, and waveform refresh. |
| `native/src/app/main.cpp` | Add smoke CLIs for request generation, view state, browser controls, latest result, and render benchmark. |
| `native/CMakeLists.txt` | Register new controller source files. |
| `native/i18n/bf_app_*.ts` | Add complete keys for M2-2 browser status and controls. |
| `tests/native/test_bf_app.py` | Add smoke tests for view state, stale result handling, browser controls, and benchmark output. |
| `scripts/common/collect_m2_format_evidence.py` | Runs optional dependency checks and writes a machine-readable summary for documentation. |
| `tests/sidecar/test_format_evidence_script.py` | Verifies evidence script records skipped dependency gates as non-evidence. |
| `docs/reports/m2-rendering-gpu-gate.md` | Record benchmark command template and actual/manual result rows. |
| `docs/reports/m2-format-acceptance.md` | Record dependency/sample evidence without treating skips as acceptance. |
| `docs/feature-parity-checklist.md` | Mark only M2-2 items that verification actually completes. |
| `docs/roadmap.md` | Update current phase wording after M2-2 verification. |

---

### Task 1: Request Generation Gate

**Files:**
- Create: `native/src/controller/request_generation.h`
- Create: `native/src/controller/request_generation.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/src/controller/file_open_controller.h`
- Modify: `native/src/controller/file_open_controller.cpp`
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: existing `FileOpenController::openInitialWindow(const QString& path)` behavior.
- Produces: `bf::RequestGeneration`, `FileOpenController::openInitialWindow(const QString& path, double windowSeconds)`, and smoke CLI `--smoke-request-generation`.

- [x] **Step 1: Write the failing request generation smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_request_generation_latest_wins():
    result = _run([str(APP), "--smoke-request-generation"])

    assert result.stdout.strip() == (
        "smoke-request-generation-ok first=1 second=2 "
        "first_current=false second_current=true after_invalidate=false"
    )
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_request_generation_latest_wins -q
```

Expected: FAIL because `--smoke-request-generation` does not exist.

- [x] **Step 3: Add the request generation primitive**

Create `native/src/controller/request_generation.h`:

```cpp
#pragma once

#include <atomic>
#include <limits>

namespace bf {

class RequestGeneration {
public:
    int next();
    bool isCurrent(int generation) const;
    void invalidate();
    int current() const;

private:
    std::atomic<int> current_{0};
};

}  // namespace bf
```

Create `native/src/controller/request_generation.cpp`:

```cpp
#include "controller/request_generation.h"

namespace bf {

int RequestGeneration::next() {
    return current_.fetch_add(1) + 1;
}

bool RequestGeneration::isCurrent(int generation) const {
    return generation == current_.load();
}

void RequestGeneration::invalidate() {
    current_.store(std::numeric_limits<int>::max());
}

int RequestGeneration::current() const {
    return current_.load();
}

}  // namespace bf
```

Register both new files in `native/CMakeLists.txt`:

```cmake
    src/controller/request_generation.cpp
    src/controller/request_generation.h
```

- [x] **Step 4: Wire `FileOpenController` to the primitive**

In `native/src/controller/file_open_controller.h`, include the new header and replace the atomic member:

```cpp
#include "controller/request_generation.h"
```

Replace:

```cpp
std::shared_ptr<std::atomic<int>> requestSerial_;
```

with:

```cpp
std::shared_ptr<RequestGeneration> requestGeneration_;
```

Change the public API to:

```cpp
void openInitialWindow(const QString& path, double windowSeconds = 10.0);
```

In `native/src/controller/file_open_controller.cpp`, construct and use the gate:

```cpp
requestGeneration_(std::make_shared<RequestGeneration>()) {}
```

In the destructor, call:

```cpp
requestGeneration_->invalidate();
```

In `openInitialWindow`, replace serial creation with:

```cpp
const std::shared_ptr<RequestGeneration> requestGeneration = requestGeneration_;
const int generation = requestGeneration->next();
const double requestedWindowSeconds = std::clamp(windowSeconds, 1.0, 120.0);
```

Replace `requestSerial` checks with:

```cpp
generation == requestGeneration->current()
```

Use the requested window:

```cpp
const double t1 = std::min(requestedWindowSeconds, opened.duration);
```

- [x] **Step 5: Add the smoke CLI**

Add this include to `native/src/app/main.cpp`:

```cpp
#include "controller/request_generation.h"
```

Add this branch before the final `QApplication app(argc, argv);`:

```cpp
if (arg == "--smoke-request-generation") {
    QCoreApplication app(argc, argv);
    bf::RequestGeneration generation;
    const int first = generation.next();
    const int second = generation.next();
    const bool firstCurrent = generation.isCurrent(first);
    const bool secondCurrent = generation.isCurrent(second);
    generation.invalidate();
    const bool afterInvalidate = generation.isCurrent(second);
    std::cout << "smoke-request-generation-ok first=" << first
              << " second=" << second
              << " first_current=" << (firstCurrent ? "true" : "false")
              << " second_current=" << (secondCurrent ? "true" : "false")
              << " after_invalidate=" << (afterInvalidate ? "true" : "false") << "\n";
    return 0;
}
```

- [x] **Step 6: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_request_generation_latest_wins -q
```

Expected: build exits 0 and test passes.

Commit:

```bash
git add native/CMakeLists.txt native/src/controller/request_generation.h native/src/controller/request_generation.cpp native/src/controller/file_open_controller.h native/src/controller/file_open_controller.cpp native/src/app/main.cpp tests/native/test_bf_app.py
git commit -m "feat: add request generation gate"
```

---

### Task 2: Waveform View State Model

**Files:**
- Create: `native/src/controller/waveform_view_state.h`
- Create: `native/src/controller/waveform_view_state.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: no prior browser state model.
- Produces: `bf::WaveformViewState`, `bf::formatWaveformViewState(const WaveformViewState&)`, and smoke CLI `--smoke-waveform-view-state`.

- [x] **Step 1: Write the failing view state smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_waveform_view_state_clamps_and_toggles():
    result = _run([str(APP), "--smoke-waveform-view-state"])

    assert result.stdout.strip() == (
        "smoke-waveform-view-state-ok window=120 amplitude=50 "
        "first_channel=3 visible=0,2,3"
    )
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_view_state_clamps_and_toggles -q
```

Expected: FAIL because `--smoke-waveform-view-state` does not exist.

- [x] **Step 3: Add `WaveformViewState`**

Create `native/src/controller/waveform_view_state.h`:

```cpp
#pragma once

#include <QString>
#include <QStringList>
#include <set>

namespace bf {

struct WaveformViewState {
    double windowSeconds = 10.0;
    double amplitudeUv = 200.0;
    int firstVisibleChannel = 0;
    std::set<int> hiddenChannels;

    void setWindowSeconds(double value);
    void setAmplitudeUv(double value);
    void setFirstVisibleChannel(int value);
    void setChannelVisible(int channel, bool visible);
    bool isChannelVisible(int channel) const;
};

QString formatVisibleChannels(const WaveformViewState& state, int nChannels);
QString formatWaveformViewState(const WaveformViewState& state, int nChannels);

}  // namespace bf
```

Create `native/src/controller/waveform_view_state.cpp`:

```cpp
#include "controller/waveform_view_state.h"

#include <algorithm>

namespace bf {

void WaveformViewState::setWindowSeconds(double value) {
    windowSeconds = std::clamp(value, 1.0, 120.0);
}

void WaveformViewState::setAmplitudeUv(double value) {
    amplitudeUv = std::clamp(value, 50.0, 5000.0);
}

void WaveformViewState::setFirstVisibleChannel(int value) {
    firstVisibleChannel = std::max(0, value);
}

void WaveformViewState::setChannelVisible(int channel, bool visible) {
    if (channel < 0) {
        return;
    }
    if (visible) {
        hiddenChannels.erase(channel);
    } else {
        hiddenChannels.insert(channel);
    }
}

bool WaveformViewState::isChannelVisible(int channel) const {
    return channel >= 0 && hiddenChannels.find(channel) == hiddenChannels.end();
}

QString formatVisibleChannels(const WaveformViewState& state, int nChannels) {
    QStringList parts;
    for (int channel = 0; channel < nChannels; ++channel) {
        if (state.isChannelVisible(channel)) {
            parts.push_back(QString::number(channel));
        }
    }
    return parts.join(",");
}

QString formatWaveformViewState(const WaveformViewState& state, int nChannels) {
    return QString("window=%1 amplitude=%2 first_channel=%3 visible=%4")
        .arg(state.windowSeconds, 0, 'f', 0)
        .arg(state.amplitudeUv, 0, 'f', 0)
        .arg(state.firstVisibleChannel)
        .arg(formatVisibleChannels(state, nChannels));
}

}  // namespace bf
```

Register both new files in `native/CMakeLists.txt`.

- [x] **Step 4: Add the smoke CLI**

Add this include to `native/src/app/main.cpp`:

```cpp
#include "controller/waveform_view_state.h"
```

Add this branch:

```cpp
if (arg == "--smoke-waveform-view-state") {
    QCoreApplication app(argc, argv);
    bf::WaveformViewState state;
    state.setWindowSeconds(500.0);
    state.setAmplitudeUv(10.0);
    state.setFirstVisibleChannel(3);
    state.setChannelVisible(1, false);
    std::cout << "smoke-waveform-view-state-ok "
              << bf::formatWaveformViewState(state, 4).toStdString() << "\n";
    return 0;
}
```

- [x] **Step 5: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_view_state_clamps_and_toggles -q
```

Expected: build exits 0 and test passes.

Commit:

```bash
git add native/CMakeLists.txt native/src/controller/waveform_view_state.h native/src/controller/waveform_view_state.cpp native/src/app/main.cpp tests/native/test_bf_app.py
git commit -m "feat: add waveform view state"
```

---

### Task 3: Visible Display Model And Amplitude-Scaled Rendering

**Files:**
- Modify: `native/src/render/waveform_slice.h`
- Modify: `native/src/render/waveform_slice.cpp`
- Modify: `native/src/ui/waveform_widget.h`
- Modify: `native/src/ui/waveform_widget.cpp`
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: `bf::WaveformViewState` from Task 2 and existing `bf::WaveformSlice`.
- Produces: `bf::WaveformDisplayModel`, `bf::waveformDisplayModel(const WaveformSlice&, const WaveformViewState&, int maxVisibleChannels)`, `WaveformWidget::setViewState(const WaveformViewState&)`, and smoke CLI `--smoke-waveform-display-model`.

- [x] **Step 1: Write the failing display model smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_waveform_display_model_filters_visible_channels():
    result = _run([str(APP), "--smoke-waveform-display-model"])

    assert result.stdout.strip() == (
        "smoke-waveform-display-model-ok channels=ch3 samples=4 "
        "window=0-0.04 amplitude=200"
    )
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_display_model_filters_visible_channels -q
```

Expected: FAIL because the smoke CLI does not exist.

- [x] **Step 3: Add the display model interface**

In `native/src/render/waveform_slice.h`, include the view state header:

```cpp
#include "controller/waveform_view_state.h"
```

Add this struct and function:

```cpp
struct WaveformDisplayModel {
    double fs = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
    double amplitudeUv = 200.0;
    QString unit = "uV";
    QStringList channelNames;
    std::vector<std::vector<float>> data;
};

WaveformDisplayModel waveformDisplayModel(
    const WaveformSlice& slice,
    const WaveformViewState& state,
    int maxVisibleChannels);
```

- [x] **Step 4: Implement visible channel filtering**

Add this to `native/src/render/waveform_slice.cpp`:

```cpp
WaveformDisplayModel waveformDisplayModel(
    const WaveformSlice& slice,
    const WaveformViewState& state,
    int maxVisibleChannels) {
    WaveformDisplayModel model;
    model.fs = slice.fs;
    model.t0 = slice.t0;
    model.t1 = slice.t1;
    model.unit = slice.unit;
    model.amplitudeUv = state.amplitudeUv;

    const int nChannels = static_cast<int>(slice.data.size());
    const int first = std::min(std::max(0, state.firstVisibleChannel), nChannels);
    const int limit = maxVisibleChannels <= 0 ? nChannels : maxVisibleChannels;
    for (int channel = first; channel < nChannels && static_cast<int>(model.data.size()) < limit; ++channel) {
        if (!state.isChannelVisible(channel)) {
            continue;
        }
        model.data.push_back(slice.data[channel]);
        if (channel < slice.channelNames.size()) {
            model.channelNames.push_back(slice.channelNames[channel]);
        } else {
            model.channelNames.push_back(QString("Ch%1").arg(channel + 1));
        }
    }
    return model;
}
```

- [x] **Step 5: Update `WaveformWidget` to consume view state**

In `native/src/ui/waveform_widget.h`, include view state and add:

```cpp
void setViewState(const WaveformViewState& state);
WaveformViewState viewState_;
```

In `native/src/ui/waveform_widget.cpp`, implement:

```cpp
void WaveformWidget::setViewState(const WaveformViewState& state) {
    viewState_ = state;
    update();
}
```

In `paintEvent`, replace direct `slice_` usage with:

```cpp
const WaveformDisplayModel model = waveformDisplayModel(slice_, viewState_, 32);
const int nChannels = static_cast<int>(model.data.size());
```

Scale each sample by fixed amplitude instead of per-row max:

```cpp
const double amplitude = std::max(1.0, model.amplitudeUv);
const double y = centerY - samples[i] / amplitude * rowHeight * 0.45;
```

Draw channel labels when available:

```cpp
painter.setPen(QPen(QColor("#CBD5E1"), 1));
if (ch < model.channelNames.size()) {
    painter.drawText(8, static_cast<int>(rowHeight * ch + 16), model.channelNames[ch]);
}
painter.setPen(QPen(QColor("#6EE7B7"), 1));
```

- [x] **Step 6: Add the display model smoke CLI**

In `native/src/app/main.cpp`, add:

```cpp
if (arg == "--smoke-waveform-display-model") {
    QCoreApplication app(argc, argv);
    bf::WaveformSlice slice;
    slice.fs = 100.0;
    slice.t0 = 0.0;
    slice.t1 = 0.04;
    slice.unit = "uV";
    slice.channelNames = {"ch1", "ch2", "ch3"};
    slice.data = {
        {0.0f, 1.0f, 2.0f, 3.0f},
        {10.0f, 11.0f, 12.0f, 13.0f},
        {20.0f, 21.0f, 22.0f, 23.0f},
    };
    bf::WaveformViewState state;
    state.setFirstVisibleChannel(1);
    state.setChannelVisible(1, false);
    state.setAmplitudeUv(200.0);
    const bf::WaveformDisplayModel model = bf::waveformDisplayModel(slice, state, 4);
    std::cout << "smoke-waveform-display-model-ok channels="
              << model.channelNames.join(",").toStdString()
              << " samples=" << (model.data.empty() ? 0 : model.data.front().size())
              << " window=" << model.t0 << "-" << model.t1
              << " amplitude=" << model.amplitudeUv << "\n";
    return 0;
}
```

- [x] **Step 7: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_display_model_filters_visible_channels tests/native/test_bf_app.py::test_bf_app_smoke_waveform_render_offscreen -q
```

Expected: build exits 0 and both tests pass.

Commit:

```bash
git add native/src/render/waveform_slice.h native/src/render/waveform_slice.cpp native/src/ui/waveform_widget.h native/src/ui/waveform_widget.cpp native/src/app/main.cpp tests/native/test_bf_app.py
git commit -m "feat: add waveform display model"
```

---

### Task 4: Browser Controls And Status Surface

**Files:**
- Modify: `native/src/ui/main_window.h`
- Modify: `native/src/ui/main_window.cpp`
- Modify: `native/src/app/main.cpp`
- Modify: `native/i18n/bf_app_zh_CN.ts`
- Modify: `native/i18n/bf_app_en_US.ts`
- Modify: `native/i18n/bf_app_pseudo.ts`
- Modify: `native/i18n/bf_app_de_DE.ts`
- Modify: `native/i18n/bf_app_es_ES.ts`
- Modify: `native/i18n/bf_app_fr_FR.ts`
- Modify: `native/i18n/bf_app_ja_JP.ts`
- Modify: `native/i18n/bf_app_ko_KR.ts`
- Modify: `native/i18n/bf_app_zh_TW.ts`
- Test: `tests/native/test_bf_app.py`
- Test: `tests/i18n/test_i18n_static.py`

**Interfaces:**
- Consumes: `WaveformViewState` and `WaveformWidget::setViewState`.
- Produces: visible browser controls, status format `app.status.file_window`, and smoke CLI `--smoke-ui-browser-controls`.

- [x] **Step 1: Write the failing browser controls smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_ui_browser_controls_show_window_and_scale():
    result = _run(
        [str(APP), "--smoke-ui-browser-controls", "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-browser-controls-ok" in result.stdout
    assert "status=CSV | 0-10 s | +/-200 uV | 2 visible channels" in result.stdout
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_browser_controls_show_window_and_scale -q
```

Expected: FAIL because `--smoke-ui-browser-controls` does not exist.

- [x] **Step 3: Add browser state and controls to `MainWindow`**

In `native/src/ui/main_window.h`, include the view state header and Qt controls:

```cpp
#include "controller/waveform_view_state.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QToolBar>
```

Add public helpers:

```cpp
void setBrowserViewState(const WaveformViewState& state);
QString browserStatusText() const;
```

Add private helpers and members:

```cpp
void buildBrowserControls();
void refreshBrowserControls();
void applyBrowserStateToWaveform();

QToolBar* browserToolbar_ = nullptr;
QDoubleSpinBox* timeWindowSpin_ = nullptr;
QDoubleSpinBox* amplitudeSpin_ = nullptr;
QScrollArea* channelScroll_ = nullptr;
QVector<QCheckBox*> channelChecks_;
WaveformViewState viewState_;
FileOpenResult currentFile_;
bool hasCurrentFile_ = false;
```

- [x] **Step 4: Implement the controls**

In `native/src/ui/main_window.cpp`, include:

```cpp
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QToolBar>
#include <QVBoxLayout>
```

In the constructor, call:

```cpp
buildBrowserControls();
```

Implement:

```cpp
void MainWindow::buildBrowserControls() {
    browserToolbar_ = addToolBar("Browser");
    timeWindowSpin_ = new QDoubleSpinBox(this);
    timeWindowSpin_->setRange(1.0, 120.0);
    timeWindowSpin_->setDecimals(0);
    timeWindowSpin_->setValue(viewState_.windowSeconds);
    amplitudeSpin_ = new QDoubleSpinBox(this);
    amplitudeSpin_->setRange(50.0, 5000.0);
    amplitudeSpin_->setDecimals(0);
    amplitudeSpin_->setValue(viewState_.amplitudeUv);
    browserToolbar_->addWidget(timeWindowSpin_);
    browserToolbar_->addWidget(amplitudeSpin_);

    connect(timeWindowSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        viewState_.setWindowSeconds(value);
        applyBrowserStateToWaveform();
    });
    connect(amplitudeSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        viewState_.setAmplitudeUv(value);
        applyBrowserStateToWaveform();
    });
}

void MainWindow::setBrowserViewState(const WaveformViewState& state) {
    viewState_ = state;
    if (timeWindowSpin_ != nullptr) {
        timeWindowSpin_->setValue(viewState_.windowSeconds);
    }
    if (amplitudeSpin_ != nullptr) {
        amplitudeSpin_->setValue(viewState_.amplitudeUv);
    }
    applyBrowserStateToWaveform();
}

void MainWindow::applyBrowserStateToWaveform() {
    waveformWidget_->setViewState(viewState_);
    if (hasCurrentFile_) {
        statusKey_ = "app.status.file_window";
        statusParams_ = {
            {"format", currentFile_.session.format},
            {"t0", currentFile_.waveform.t0},
            {"t1", currentFile_.waveform.t1},
            {"amplitude", viewState_.amplitudeUv},
            {"n_visible", static_cast<int>(waveformDisplayModel(currentFile_.waveform, viewState_, 32).data.size())},
        };
        retranslateUi();
    }
}

QString MainWindow::browserStatusText() const {
    return statusBar()->currentMessage();
}
```

Update `showFileOpenResult` to set `currentFile_`, `hasCurrentFile_`, and call `applyBrowserStateToWaveform()` after `setSlice`.

- [x] **Step 5: Add i18n keys**

Add this key to every `native/i18n/bf_app_*.ts` file:

```xml
<message><source>app.status.file_window</source><translation>{format} | {t0}-{t1} s | +/-{amplitude} uV | {n_visible} visible channels</translation></message>
```

For `native/i18n/bf_app_zh_CN.ts`, use:

```xml
<message><source>app.status.file_window</source><translation>{format} | {t0}-{t1} 秒 | +/-{amplitude} uV | {n_visible} 个可见通道</translation></message>
```

For `native/i18n/bf_app_pseudo.ts`, use:

```xml
<message><source>app.status.file_window</source><translation>[!! {format} | {t0}-{t1} s | +/-{amplitude} uV | {n_visible} viisiiblee chaanneels !!]</translation></message>
```

- [x] **Step 6: Add the browser controls smoke CLI**

Add this branch to `native/src/app/main.cpp`:

```cpp
if (arg == "--smoke-ui-browser-controls") {
    QApplication app(argc, argv);
    bf::I18nService i18n(repoRoot);
    QString i18nError;
    if (!i18n.loadLocale("en_US", &i18nError)) {
        std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
        return 1;
    }
    bf::MainWindow window(version, &i18n);
    bf::FileOpenResult result;
    result.session.ok = true;
    result.session.format = "CSV";
    result.session.fs = 100.0;
    result.session.nChannels = 2;
    result.session.nSamples = 1000;
    result.waveform.fs = 100.0;
    result.waveform.t0 = 0.0;
    result.waveform.t1 = 10.0;
    result.waveform.channelNames = {"ch1", "ch2"};
    result.waveform.data = {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, -1.0f}};
    window.showFileOpenResult(result);
    std::cout << "smoke-ui-browser-controls-ok status=" << window.browserStatusText().toStdString() << "\n";
    return 0;
}
```

- [x] **Step 7: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_browser_controls_show_window_and_scale tests/i18n/test_i18n_static.py -q
```

Expected: build exits 0 and tests pass.

Commit:

```bash
git add native/src/ui/main_window.h native/src/ui/main_window.cpp native/src/app/main.cpp native/i18n/bf_app_*.ts tests/native/test_bf_app.py
git commit -m "feat: add static browser controls"
```

---

### Task 5: Window Request And Latest-Result Smoke

**Files:**
- Modify: `native/src/controller/file_open_controller.h`
- Modify: `native/src/controller/file_open_controller.cpp`
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: `RequestGeneration` and `WaveformViewState`.
- Produces: `FileOpenController::openWindow(const QString& path, double t0, double windowSeconds)` and smoke CLI `--smoke-ui-open-latest`.

- [x] **Step 1: Write the failing latest-result smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_ui_open_latest_keeps_newer_result():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui-open-latest", str(sample), "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-open-latest-ok t0=0.02 t1=0.05" in result.stdout
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_open_latest_keeps_newer_result -q
```

Expected: FAIL because `--smoke-ui-open-latest` does not exist.

- [x] **Step 3: Add explicit window request API**

In `native/src/controller/file_open_controller.h`, add:

```cpp
void openWindow(const QString& path, double t0, double windowSeconds);
```

In `native/src/controller/file_open_controller.cpp`, make `openInitialWindow` delegate:

```cpp
void FileOpenController::openInitialWindow(const QString& path, double windowSeconds) {
    openWindow(path, 0.0, windowSeconds);
}
```

Move the existing thread body into `openWindow`. Compute the requested interval:

```cpp
const double requestedT0 = std::max(0.0, t0);
const double requestedWindowSeconds = std::clamp(windowSeconds, 1.0, 120.0);
const double requestedT1 = requestedT0 + requestedWindowSeconds;
```

After open:

```cpp
const double sliceT0 = std::min(requestedT0, opened.duration);
const double sliceT1 = std::min(requestedT1, opened.duration);
```

If `sliceT1 <= sliceT0`, emit:

```cpp
makeError("reader.slice_out_of_range", {
    {"handle", opened.handle},
    {"t0", sliceT0},
    {"t1", sliceT1},
    {"duration", opened.duration},
})
```

Then call:

```cpp
SidecarSliceResult slice = sidecar.getSlice(opened.handle, sliceT0, sliceT1, 30000);
```

- [x] **Step 4: Add latest-result smoke CLI**

Add this branch to `native/src/app/main.cpp`:

```cpp
if (arg == "--smoke-ui-open-latest" && i + 1 < argc) {
    QApplication app(argc, argv);
    bf::I18nService i18n(repoRoot);
    QString i18nError;
    if (!i18n.loadLocale("en_US", &i18nError)) {
        std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
        return 1;
    }
    bf::MainWindow window(version, &i18n);
    bf::FileOpenController controller(repoRoot, version);
    int exitCode = 1;
    QObject::connect(&controller, &bf::FileOpenController::loaded, &window, [&](const bf::FileOpenResult& result) {
        window.showFileOpenResult(result);
        if (result.waveform.t0 == 0.02 && result.waveform.t1 == 0.05) {
            std::cout << "smoke-ui-open-latest-ok t0=" << result.waveform.t0
                      << " t1=" << result.waveform.t1 << "\n";
            exitCode = 0;
            app.quit();
        }
    });
    QObject::connect(&controller, &bf::FileOpenController::failed, &window, [&](const bf::AppError& error) {
        std::cerr << "smoke-ui-open-latest-failed code=" << error.code.toStdString() << "\n";
        exitCode = 1;
        app.quit();
    });
    const QString path = QString::fromLocal8Bit(argv[i + 1]);
    controller.openWindow(path, 0.0, 0.03);
    controller.openWindow(path, 0.02, 0.03);
    QTimer::singleShot(5000, &app, [&]() {
        std::cerr << "smoke-ui-open-latest-failed code=timeout\n";
        exitCode = 1;
        app.quit();
    });
    app.exec();
    return exitCode;
}
```

- [x] **Step 5: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_open_latest_keeps_newer_result tests/native/test_bf_app.py::test_bf_app_smoke_ui_open_generated_csv_fixture -q
```

Expected: build exits 0 and tests pass.

Commit:

```bash
git add native/src/controller/file_open_controller.h native/src/controller/file_open_controller.cpp native/src/app/main.cpp tests/native/test_bf_app.py
git commit -m "feat: harden static browser window requests"
```

---

### Task 6: Render Benchmark CLI And GPU Gate Report

**Files:**
- Modify: `native/src/app/main.cpp`
- Modify: `docs/reports/m2-rendering-gpu-gate.md`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: `WaveformWidget`, `WaveformSlice`, and `WaveformViewState`.
- Produces: smoke CLI `--smoke-waveform-benchmark --channels N --fs N --seconds N --frames N`.

- [x] **Step 1: Write the failing benchmark smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_waveform_benchmark_reports_development_result():
    result = _run(
        [
            str(APP),
            "--smoke-waveform-benchmark",
            "--channels",
            "64",
            "--fs",
            "1000",
            "--seconds",
            "4",
            "--frames",
            "3",
        ],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-waveform-benchmark-ok channels=64 fs=1000 seconds=4 frames=3" in result.stdout
    assert "gate=development-signal-only" in result.stdout
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_benchmark_reports_development_result -q
```

Expected: FAIL because the benchmark CLI does not exist.

- [x] **Step 3: Add the benchmark CLI**

In `native/src/app/main.cpp`, add:

```cpp
#include <QElapsedTimer>
```

Add a helper near the top of `main`:

```cpp
auto valueAfter = [&](int start, const QString& key, const QString& fallback) {
    for (int j = start; j + 1 < argc; ++j) {
        if (QString::fromLocal8Bit(argv[j]) == key) {
            return QString::fromLocal8Bit(argv[j + 1]);
        }
    }
    return fallback;
};
```

Add this branch:

```cpp
if (arg == "--smoke-waveform-benchmark") {
    QApplication app(argc, argv);
    const int channels = valueAfter(i + 1, "--channels", "256").toInt();
    const int fs = valueAfter(i + 1, "--fs", "1000").toInt();
    const int seconds = valueAfter(i + 1, "--seconds", "4").toInt();
    const int frames = valueAfter(i + 1, "--frames", "30").toInt();
    bf::WaveformWidget widget;
    bf::WaveformSlice slice;
    slice.fs = fs;
    slice.t0 = 0.0;
    slice.t1 = seconds;
    for (int ch = 0; ch < channels; ++ch) {
        slice.channelNames.push_back(QString("Ch%1").arg(ch + 1));
        std::vector<float> samples;
        samples.reserve(fs * seconds);
        for (int sample = 0; sample < fs * seconds; ++sample) {
            samples.push_back(static_cast<float>(((sample + ch) % 200) - 100));
        }
        slice.data.push_back(std::move(samples));
    }
    bf::WaveformViewState state;
    state.setWindowSeconds(seconds);
    state.setAmplitudeUv(200.0);
    widget.resize(1280, 720);
    widget.setViewState(state);
    widget.setSlice(slice);

    QImage image(widget.size(), QImage::Format_ARGB32);
    QElapsedTimer timer;
    timer.start();
    for (int frame = 0; frame < frames; ++frame) {
        image.fill(QColor("#111827"));
        widget.render(&image);
    }
    const double elapsedSeconds = std::max(0.001, timer.elapsed() / 1000.0);
    const double fps = frames / elapsedSeconds;
    std::cout << "smoke-waveform-benchmark-ok channels=" << channels
              << " fs=" << fs
              << " seconds=" << seconds
              << " frames=" << frames
              << " fps=" << fps
              << " gate=development-signal-only\n";
    return 0;
}
```

- [x] **Step 4: Update the GPU gate report**

Append this section to `docs/reports/m2-rendering-gpu-gate.md`:

```markdown
## Benchmark Command

Development smoke command:

```bash
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 30
```

This command is a development signal only when run offscreen, under WSL, Xvfb, or Mesa software rendering. It does not close the real GPU/display gate.

Real GPU gate command template:

```bash
native/build/bf_app --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 120
```

## Result Log

| Date | Environment | Channels | fs | Window | FPS | CPU | GPU | Interaction Notes | Gate |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-07-09 | Not yet run on real GPU/display | 256 | 1000 Hz | 4 s | not recorded | not recorded | not recorded | not recorded | Open |
```

- [x] **Step 5: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_benchmark_reports_development_result -q
```

Expected: build exits 0 and test passes. Do not mark the real GPU gate complete unless the command is run on a real GPU/display environment and the result meets the acceptance target.

Commit:

```bash
git add native/src/app/main.cpp docs/reports/m2-rendering-gpu-gate.md tests/native/test_bf_app.py
git commit -m "feat: add waveform render benchmark smoke"
```

---

### Task 7: Format Evidence Script

**Files:**
- Create: `scripts/common/collect_m2_format_evidence.py`
- Create: `tests/sidecar/test_format_evidence_script.py`
- Modify: `docs/reports/m2-format-acceptance.md`

**Interfaces:**
- Consumes: optional dependencies `h5py==3.16.0`, `mne==1.12.1`, and existing format tests.
- Produces: CLI `python3 scripts/common/collect_m2_format_evidence.py --dry-run` and explicit evidence status values `accepted`, `dependency-skipped`, `sample-skipped`, `not-implemented`.

- [x] **Step 1: Write the failing evidence script test**

Create `tests/sidecar/test_format_evidence_script.py`:

```python
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_collect_m2_format_evidence_dry_run_reports_skips_as_non_evidence():
    result = subprocess.run(
        ["python3", "scripts/common/collect_m2_format_evidence.py", "--dry-run"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )

    data = json.loads(result.stdout)

    assert data["CSV"]["status"] == "accepted"
    assert data["HDF5"]["status"] in {"accepted", "dependency-skipped"}
    assert data["EDF"]["status"] in {"accepted", "dependency-skipped", "sample-skipped"}
    assert data["HDF5"]["skip_is_acceptance_evidence"] is False
    assert data["EDF"]["skip_is_acceptance_evidence"] is False
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/sidecar/test_format_evidence_script.py -q
```

Expected: FAIL because `scripts/common/collect_m2_format_evidence.py` does not exist.

- [x] **Step 3: Add the evidence script**

Create `scripts/common/collect_m2_format_evidence.py`:

```python
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
            "status": "accepted" if (mne_available and edf_sample_available) else (
                "dependency-skipped" if not mne_available else "sample-skipped"
            ),
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
```

- [x] **Step 4: Update the format acceptance report**

Add this section to `docs/reports/m2-format-acceptance.md`:

```markdown
## Evidence Collection Command

```bash
python3 scripts/common/collect_m2_format_evidence.py --dry-run
```

The script reports dependency and sample skips as non-evidence. Only `accepted` rows with recorded tests or manual evidence can be reflected as completed checklist items.
```

- [x] **Step 5: Run verification and commit**

Run:

```bash
python3 -m pytest tests/sidecar/test_format_evidence_script.py tests/sidecar/test_hdf5_reader.py tests/sidecar/test_mne_reader.py -q
```

Expected: evidence script test passes; HDF5/MNE tests pass only when dependencies/samples exist, otherwise skip without being counted as acceptance evidence.

Commit:

```bash
git add scripts/common/collect_m2_format_evidence.py tests/sidecar/test_format_evidence_script.py docs/reports/m2-format-acceptance.md
git commit -m "docs: add m2 format evidence collection"
```

---

### Task 8: M2-2 Documentation And Full Verification

**Files:**
- Modify: `docs/feature-parity-checklist.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/reports/m2-rendering-gpu-gate.md`
- Modify: `docs/reports/m2-format-acceptance.md`
- Modify: `docs/superpowers/plans/2026-07-09-m2-2-static-waveform-browser.md`

**Interfaces:**
- Consumes: completed Tasks 1-7.
- Produces: explicit M2-2 status, no overclaiming of full M2, and a verified branch ready for review or merge.

- [x] **Step 1: Update feature checklist narrowly**

In `docs/feature-parity-checklist.md`, under "波形显示 — M2", mark only items whose tests and implementation passed:

```markdown
- [x] 多通道波形（垂直滚动）
- [x] 通道显隐
- [x] 时间窗 1–120s（默认 10s）
- [x] 幅值缩放（默认 ±200μV）
```

Keep this unchecked unless real GPU/display evidence is recorded and meets the gate:

```markdown
- [ ] 真实 GPU/显示环境复测记录已写入 `docs/reports/m2-rendering-gpu-gate.md`
```

Do not mark DSP, playback, annotations, video, export, recording, hardware acquisition, or sample-gated formats complete.

- [x] **Step 2: Update roadmap current phase**

In `docs/roadmap.md`, update "当前阶段" to:

```markdown
当前阶段进入 M2-2：静态波形浏览交互、latest-request-wins 加固、渲染基准入口和格式验收证据收口。M2-2 仍不代表完整 M2 验收；真实 GPU/显示环境门禁只有在 `docs/reports/m2-rendering-gpu-gate.md` 记录通过后才能关闭。
```

- [x] **Step 3: Record verification status in reports**

In `docs/reports/m2-rendering-gpu-gate.md`, keep the gate `Open` unless a real GPU/display result was recorded. If only offscreen smoke ran, add:

```markdown
Offscreen benchmark smoke passed in development, but this does not close the real GPU/display gate.
```

In `docs/reports/m2-format-acceptance.md`, copy the evidence script result summary. Preserve this statement:

```markdown
Skipped dependency-gated tests are not format acceptance evidence.
```

- [x] **Step 4: Run full verification**

Run:

```bash
python3 -m pytest tests/fixtures tests/sidecar tests/native tests/contracts tests/i18n -q
cmake --build native/build
./scripts/wsl/verify_m1_foundation.sh --fast
python3 scripts/common/collect_m2_format_evidence.py --dry-run
git diff --check HEAD
```

Expected:

```text
all non-skipped tests pass
native build exits 0
M1 foundation verification passed
format evidence script exits 0
git diff --check exits 0
```

- [x] **Step 5: Mark plan execution checkboxes**

In this plan file, mark completed task steps with `[x]` only after their commands pass. Do not mark real GPU acceptance complete unless the real environment result exists.

- [x] **Step 6: Commit documentation and verification status**

Commit:

```bash
git add docs/feature-parity-checklist.md docs/roadmap.md docs/reports/m2-rendering-gpu-gate.md docs/reports/m2-format-acceptance.md docs/superpowers/plans/2026-07-09-m2-2-static-waveform-browser.md
git commit -m "docs: record m2-2 implementation status"
```

## Self-Review

Spec coverage:
- Static browser controls are covered by Tasks 2-4.
- Async stale-result handling is covered by Tasks 1 and 5.
- Real GPU/display gate is covered by Task 6 and remains separate from offscreen smoke.
- Format evidence and skip semantics are covered by Task 7.
- Documentation closure is covered by Task 8.

Placeholder scan:
- This plan contains no deferred implementation placeholders.
- It does not mark sample-gated formats or real GPU acceptance complete without evidence.

Type consistency:
- `WaveformViewState` is produced in Task 2 and consumed by Tasks 3-6.
- `RequestGeneration` is produced in Task 1 and consumed by Task 5 through `FileOpenController`.
- `WaveformDisplayModel` is produced in Task 3 and consumed by `WaveformWidget` and MainWindow status logic.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-09-m2-2-static-waveform-browser.md`.

Two execution options:

1. Subagent-Driven (recommended) - dispatch a fresh subagent per task, review between tasks, fast iteration.
2. Inline Execution - execute tasks in this session using executing-plans, batch execution with checkpoints.
