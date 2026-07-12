# M2-3 GPU Browser Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the M2 static-browser acceptance gap by hardening channel/window browser controls, improving benchmark evidence capture, and keeping the real GPU gate honestly documented.

**Architecture:** Extend the existing M2-2 browser boundaries. `WaveformViewState` remains the browser state source, `WaveformDisplayModel` remains the render-facing model, `FileOpenController::openWindow` remains the sidecar slice request entry point, and `RequestGeneration` remains the stale-result guard. UI code may wire simple controls, but file IO, format parsing, DSP, export, playback, and recording stay out of `MainWindow`.

**Tech Stack:** C++17, Qt6 Core/Widgets, CMake, Python 3.10+, pytest, existing Python sidecar IPC, existing docs under `docs/reports/`.

## Global Constraints

- Work in `boundfree-eeg-native`; do not modify `../boundfree-eeg-upper-computer`.
- Current product version remains `0.1.0-dev`; ordinary feature work must not change `version.json`.
- File format ownership remains in the Python sidecar; Native must not directly parse scientific file formats.
- Sidecar and backend errors remain `code + params`; UI maps errors through i18n.
- EEG data shape remains `[n_channels, n_samples]`, unit `uV`.
- M2-3 must not implement DSP, playback, marker editing, video sync, export, recording, packaging, or hardware acquisition.
- Offscreen, WSL, Xvfb, or Mesa benchmark results are development signals only and must not close the real GPU/display gate.
- Real GPU acceptance requires 64, 128, and 256 channels at 1 kHz with a 4 s visible window, with OS/GPU/driver/Qt/command/FPS/CPU/GPU/interaction notes recorded.
- M2 must not be marked complete unless the real GPU/display gate is recorded as passed.
- Optional dependency skips and sample skips are not acceptance evidence.
- Follow TDD: write the failing test, verify it fails, implement the minimal behavior, verify it passes, then commit.

---

## File Structure

| Path | Responsibility |
| --- | --- |
| `native/src/app/main.cpp` | Add smoke CLIs for benchmark JSON, channel controls, window request, and stale window behavior. |
| `native/src/ipc/sidecar_client.h` | Add source file path to `SidecarOpenResult` so UI/controller wiring can request later windows for the active file. |
| `native/src/ipc/sidecar_client.cpp` | Populate `SidecarOpenResult::path` in `SidecarClient::openFile`. |
| `native/src/ui/main_window.h` | Add channel controls, first-visible-channel control, window request handler API, and smoke-readable browser summaries. |
| `native/src/ui/main_window.cpp` | Build channel panel, wire visibility/window controls to `WaveformViewState`, update status, and dispatch window requests. |
| `tests/native/test_bf_app.py` | Add smoke tests for JSON benchmark output, channel browser controls, window request wiring, and latest-window behavior. |
| `docs/reports/m2-rendering-gpu-gate.md` | Record JSON benchmark command and real GPU evidence checklist; keep gate open unless real evidence exists. |
| `docs/feature-parity-checklist.md` | Mark only browser items verified by M2-3 implementation and evidence. |
| `docs/roadmap.md` | Update current stage after M2-3 verification. |
| `docs/superpowers/plans/2026-07-12-m2-3-gpu-browser-acceptance.md` | Track plan execution checkboxes after verification. |

---

### Task 1: Benchmark JSON Output And Gate Checklist

**Files:**
- Modify: `native/src/app/main.cpp`
- Modify: `docs/reports/m2-rendering-gpu-gate.md`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: existing smoke CLI `--smoke-waveform-benchmark --channels N --fs N --seconds N --frames N`.
- Produces: optional `--json` flag for `--smoke-waveform-benchmark`, with JSON fields `channels`, `fs`, `seconds`, `frames`, `fps`, and `gate`.

- [x] **Step 1: Write the failing benchmark JSON smoke test**

Append this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_waveform_benchmark_json_reports_gate_status():
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
            "2",
            "--json",
        ],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    data = json.loads(result.stdout)
    assert data["channels"] == 64
    assert data["fs"] == 1000
    assert data["seconds"] == 4
    assert data["frames"] == 2
    assert data["fps"] > 0
    assert data["gate"] == "development-signal-only"
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_benchmark_json_reports_gate_status -q
```

Expected: FAIL because `--json` currently still returns text output and `json.loads(result.stdout)` raises `json.JSONDecodeError`.

- [x] **Step 3: Implement JSON output in the benchmark branch**

In `native/src/app/main.cpp`, add this include next to the existing JSON include:

```cpp
#include <QJsonObject>
```

Add this helper after `valueAfter`:

```cpp
    auto hasFlag = [&](int start, const QString& key) {
        for (int j = start; j < argc; ++j) {
            if (QString::fromLocal8Bit(argv[j]) == key) {
                return true;
            }
        }
        return false;
    };
```

Inside the `if (arg == "--smoke-waveform-benchmark")` branch, add a local flag after reading `frames`:

```cpp
            const bool jsonOutput = hasFlag(i + 1, "--json");
```

Replace the existing final `std::cout << "smoke-waveform-benchmark-ok ..."` block with:

```cpp
            if (jsonOutput) {
                const QJsonObject payload{
                    {"channels", channels},
                    {"fs", fs},
                    {"seconds", seconds},
                    {"frames", frames},
                    {"fps", fps},
                    {"gate", "development-signal-only"},
                };
                std::cout << QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString() << "\n";
            } else {
                std::cout << "smoke-waveform-benchmark-ok channels=" << channels
                          << " fs=" << fs
                          << " seconds=" << seconds
                          << " frames=" << frames
                          << " fps=" << fps
                          << " gate=development-signal-only\n";
            }
            return 0;
```

`QJsonDocument` is already included in `main.cpp`; `QJsonObject` must be included directly in this task because the benchmark branch constructs a `QJsonObject`.

- [x] **Step 4: Update the GPU gate report with the JSON command**

In `docs/reports/m2-rendering-gpu-gate.md`, add this paragraph under `## Benchmark Command` after the development smoke command:

```markdown
Machine-readable development smoke command:

```bash
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-waveform-benchmark --channels 256 --fs 1000 --seconds 4 --frames 30 --json
```
```

Add this checklist under the real GPU command template:

```markdown
Real GPU result rows must include:

- OS and display environment.
- GPU model and driver version.
- Qt version and command.
- FPS for 64, 128, and 256 channels.
- CPU and GPU utilization source.
- Scroll, zoom, and channel-visibility subjective notes.
```

- [x] **Step 5: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_waveform_benchmark_json_reports_gate_status tests/native/test_bf_app.py::test_bf_app_smoke_waveform_benchmark_reports_development_result -q
git diff --check HEAD
```

Expected: build exits 0; both benchmark smoke tests pass; diff check exits 0.

Commit:

```bash
git add native/src/app/main.cpp docs/reports/m2-rendering-gpu-gate.md tests/native/test_bf_app.py
git commit -m "feat: add waveform benchmark json output"
```

---

### Task 2: Channel Browser Controls

**Files:**
- Modify: `native/src/ui/main_window.h`
- Modify: `native/src/ui/main_window.cpp`
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: `WaveformViewState::setFirstVisibleChannel(int)`, `WaveformViewState::setChannelVisible(int, bool)`, `formatVisibleChannels(const WaveformViewState&, int)`, and `WaveformDisplayModel waveformDisplayModel(...)`.
- Produces: `MainWindow::setFirstVisibleChannelForBrowser(int)`, `MainWindow::setChannelVisibleForBrowser(int, bool)`, `MainWindow::browserControlSummary() const`, and smoke CLI `--smoke-ui-channel-browser`.

- [x] **Step 1: Write the failing channel browser smoke test**

Append this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_ui_channel_browser_updates_visible_channels():
    result = _run(
        [str(APP), "--smoke-ui-channel-browser", "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-channel-browser-ok" in result.stdout
    assert "summary=first=2 visible=2,3 checks=ch1:on,ch2:off,ch3:on,ch4:on" in result.stdout
    assert "status=CSV | 0-10 s | +/-200 uV | 2 visible channels" in result.stdout
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_channel_browser_updates_visible_channels -q
```

Expected: FAIL because `--smoke-ui-channel-browser` is unknown.

- [x] **Step 3: Add channel control members to `MainWindow`**

In `native/src/ui/main_window.h`, add includes:

```cpp
#include <QCheckBox>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
```

Add public methods after `QString browserStatusText() const;`:

```cpp
    void setFirstVisibleChannelForBrowser(int channel);
    void setChannelVisibleForBrowser(int channel, bool visible);
    QString browserControlSummary() const;
```

Add private helpers after `void applyBrowserStateToWaveform();`:

```cpp
    void rebuildChannelControls();
    QString channelCheckSummary() const;
    int channelCount() const;
```

Add private members after `QDoubleSpinBox* amplitudeSpin_ = nullptr;`:

```cpp
    QSpinBox* firstChannelSpin_ = nullptr;
    QScrollArea* channelScroll_ = nullptr;
    QWidget* channelListWidget_ = nullptr;
    QVBoxLayout* channelListLayout_ = nullptr;
    QVector<QCheckBox*> channelChecks_;
    bool refreshingBrowserControls_ = false;
```

- [x] **Step 4: Build channel controls in `MainWindow`**

In `native/src/ui/main_window.cpp`, add includes:

```cpp
#include <QCheckBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
```

In `MainWindow::buildBrowserControls()`, after configuring `amplitudeSpin_`, create `firstChannelSpin_` and the channel scroll area:

```cpp
    firstChannelSpin_ = new QSpinBox(this);
    firstChannelSpin_->setRange(0, 0);
    firstChannelSpin_->setValue(viewState_.firstVisibleChannel);

    browserToolbar_->addWidget(timeWindowSpin_);
    browserToolbar_->addWidget(amplitudeSpin_);
    browserToolbar_->addWidget(firstChannelSpin_);

    channelScroll_ = new QScrollArea(this);
    channelScroll_->setWidgetResizable(true);
    channelScroll_->setMaximumWidth(180);
    channelListWidget_ = new QWidget(channelScroll_);
    channelListLayout_ = new QVBoxLayout(channelListWidget_);
    channelListLayout_->setContentsMargins(4, 4, 4, 4);
    channelListLayout_->setSpacing(2);
    channelScroll_->setWidget(channelListWidget_);
    browserToolbar_->addWidget(channelScroll_);
```

Keep the existing `timeWindowSpin_` and `amplitudeSpin_` setup, but remove the earlier duplicate `browserToolbar_->addWidget(timeWindowSpin_)` and `browserToolbar_->addWidget(amplitudeSpin_)` calls so each widget is added once.

Add this connection after the amplitude spin connection:

```cpp
    connect(firstChannelSpin_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
        if (refreshingBrowserControls_) {
            return;
        }
        viewState_.setFirstVisibleChannel(value);
        applyBrowserStateToWaveform();
    });
```

- [x] **Step 5: Implement channel helper methods**

In `native/src/ui/main_window.cpp`, add these methods before `retranslateUi()`:

```cpp
int MainWindow::channelCount() const {
    if (!hasCurrentFile_) {
        return 0;
    }
    return static_cast<int>(currentFile_.waveform.data.size());
}

void MainWindow::rebuildChannelControls() {
    if (channelListLayout_ == nullptr) {
        return;
    }

    while (QLayoutItem* item = channelListLayout_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    channelChecks_.clear();

    if (!hasCurrentFile_) {
        return;
    }

    const int nChannels = channelCount();
    for (int channel = 0; channel < nChannels; ++channel) {
        const QString label = channel < currentFile_.waveform.channelNames.size()
            ? currentFile_.waveform.channelNames[channel]
            : QString("Ch%1").arg(channel + 1);
        auto* check = new QCheckBox(label, channelListWidget_);
        check->setChecked(viewState_.isChannelVisible(channel));
        connect(check, &QCheckBox::toggled, this, [this, channel](bool checked) {
            if (refreshingBrowserControls_) {
                return;
            }
            viewState_.setChannelVisible(channel, checked);
            applyBrowserStateToWaveform();
        });
        channelListLayout_->addWidget(check);
        channelChecks_.push_back(check);
    }
    channelListLayout_->addStretch(1);
}

QString MainWindow::channelCheckSummary() const {
    QStringList parts;
    for (int channel = 0; channel < channelChecks_.size(); ++channel) {
        const QCheckBox* check = channelChecks_[channel];
        parts.push_back(QString("%1:%2").arg(check->text(), check->isChecked() ? "on" : "off"));
    }
    return parts.join(",");
}
```

- [x] **Step 6: Refresh controls and expose browser summary**

Replace `MainWindow::refreshBrowserControls()` with:

```cpp
void MainWindow::refreshBrowserControls() {
    refreshingBrowserControls_ = true;
    if (timeWindowSpin_ != nullptr) {
        QSignalBlocker blocker(timeWindowSpin_);
        timeWindowSpin_->setValue(viewState_.windowSeconds);
    }
    if (amplitudeSpin_ != nullptr) {
        QSignalBlocker blocker(amplitudeSpin_);
        amplitudeSpin_->setValue(viewState_.amplitudeUv);
    }
    if (firstChannelSpin_ != nullptr) {
        QSignalBlocker blocker(firstChannelSpin_);
        const int maxFirst = std::max(0, channelCount() - 1);
        firstChannelSpin_->setRange(0, maxFirst);
        firstChannelSpin_->setValue(std::min(viewState_.firstVisibleChannel, maxFirst));
    }
    refreshingBrowserControls_ = false;
    rebuildChannelControls();
}
```

Add these public methods near `browserStatusText()`:

```cpp
void MainWindow::setFirstVisibleChannelForBrowser(int channel) {
    viewState_.setFirstVisibleChannel(channel);
    refreshBrowserControls();
    applyBrowserStateToWaveform();
}

void MainWindow::setChannelVisibleForBrowser(int channel, bool visible) {
    viewState_.setChannelVisible(channel, visible);
    refreshBrowserControls();
    applyBrowserStateToWaveform();
}

QString MainWindow::browserControlSummary() const {
    return QString("first=%1 visible=%2 checks=%3")
        .arg(viewState_.firstVisibleChannel)
        .arg(formatVisibleChannels(viewState_, channelCount()))
        .arg(channelCheckSummary());
}
```

- [x] **Step 7: Add the channel browser smoke CLI**

In `native/src/app/main.cpp`, add this branch before `--smoke-parse-slice-payload`:

```cpp
        if (arg == "--smoke-ui-channel-browser") {
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
            result.session.nChannels = 4;
            result.session.nSamples = 1000;
            result.waveform.fs = 100.0;
            result.waveform.t0 = 0.0;
            result.waveform.t1 = 10.0;
            result.waveform.channelNames = {"ch1", "ch2", "ch3", "ch4"};
            result.waveform.data = {
                {0.0f, 1.0f, 0.0f},
                {1.0f, 0.0f, -1.0f},
                {2.0f, 1.0f, 0.0f},
                {3.0f, 2.0f, 1.0f},
            };
            window.showFileOpenResult(result);
            window.setChannelVisibleForBrowser(1, false);
            window.setFirstVisibleChannelForBrowser(2);
            std::cout << "smoke-ui-channel-browser-ok summary="
                      << window.browserControlSummary().toStdString()
                      << " status=" << window.browserStatusText().toStdString() << "\n";
            return 0;
        }
```

- [x] **Step 8: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_channel_browser_updates_visible_channels tests/native/test_bf_app.py::test_bf_app_smoke_ui_browser_controls_show_window_and_scale -q
git diff --check HEAD
```

Expected: build exits 0; both tests pass; diff check exits 0.

Commit:

```bash
git add native/src/ui/main_window.h native/src/ui/main_window.cpp native/src/app/main.cpp tests/native/test_bf_app.py
git commit -m "feat: add static browser channel controls"
```

---

### Task 3: Time Window Request Wiring

**Files:**
- Modify: `native/src/ipc/sidecar_client.h`
- Modify: `native/src/ipc/sidecar_client.cpp`
- Modify: `native/src/ui/main_window.h`
- Modify: `native/src/ui/main_window.cpp`
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: `FileOpenController::openWindow(const QString& path, double t0, double windowSeconds)` and `WaveformViewState::windowSeconds`.
- Produces: `SidecarOpenResult::path`, `MainWindow::setWindowRequestHandler(std::function<void(const QString&, double, double)>)`, `MainWindow::setBrowserWindowSecondsForBrowser(double)`, and smoke CLI `--smoke-ui-window-request`.

- [x] **Step 1: Write the failing window request smoke test**

Append this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_ui_window_request_reports_requested_interval():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui-window-request", str(sample), "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert f"smoke-ui-window-request-ok path={sample} t0=0 window=4" in result.stdout
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_window_request_reports_requested_interval -q
```

Expected: FAIL because `--smoke-ui-window-request` is unknown.

- [x] **Step 3: Add source path to sidecar open result**

In `native/src/ipc/sidecar_client.h`, add this field to `struct SidecarOpenResult` after `QString handle;`:

```cpp
    QString path;
```

In `native/src/ipc/sidecar_client.cpp`, inside `SidecarClient::openFile`, set it immediately after declaring `result`:

```cpp
    result.path = path;
```

- [x] **Step 4: Add window request handler API to `MainWindow`**

In `native/src/ui/main_window.h`, add include:

```cpp
#include <functional>
```

Add this public method after `QString browserControlSummary() const;`:

```cpp
    void setWindowRequestHandler(std::function<void(const QString&, double, double)> handler);
    void setBrowserWindowSecondsForBrowser(double seconds);
```

Add this private member after `bool refreshingBrowserControls_ = false;`:

```cpp
    std::function<void(const QString&, double, double)> windowRequestHandler_;
```

- [x] **Step 5: Dispatch window requests from time-window changes**

In `native/src/ui/main_window.cpp`, update the time-window spin connection in `buildBrowserControls()` to:

```cpp
    connect(timeWindowSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (refreshingBrowserControls_) {
            return;
        }
        viewState_.setWindowSeconds(value);
        applyBrowserStateToWaveform();
        if (hasCurrentFile_ && windowRequestHandler_) {
            windowRequestHandler_(currentFile_.session.path, currentFile_.waveform.t0, viewState_.windowSeconds);
        }
    });
```

Add these methods near `setChannelVisibleForBrowser`:

```cpp
void MainWindow::setWindowRequestHandler(std::function<void(const QString&, double, double)> handler) {
    windowRequestHandler_ = std::move(handler);
}

void MainWindow::setBrowserWindowSecondsForBrowser(double seconds) {
    if (timeWindowSpin_ != nullptr) {
        timeWindowSpin_->setValue(seconds);
        return;
    }
    viewState_.setWindowSeconds(seconds);
    applyBrowserStateToWaveform();
}
```

Because `std::move` is used, add this include to `native/src/ui/main_window.cpp`:

```cpp
#include <utility>
```

- [x] **Step 6: Add the window request smoke CLI**

In `native/src/app/main.cpp`, add this branch before `--smoke-ui-channel-browser`:

```cpp
        if (arg == "--smoke-ui-window-request" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }
            const QString path = QString::fromLocal8Bit(argv[i + 1]);
            bf::MainWindow window(version, &i18n);
            QString requestedPath;
            double requestedT0 = -1.0;
            double requestedWindow = -1.0;
            window.setWindowRequestHandler([&](const QString& activePath, double t0, double windowSeconds) {
                requestedPath = activePath;
                requestedT0 = t0;
                requestedWindow = windowSeconds;
            });
            bf::FileOpenResult result;
            result.session.ok = true;
            result.session.path = path;
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
            window.setBrowserWindowSecondsForBrowser(4.0);
            std::cout << "smoke-ui-window-request-ok path=" << requestedPath.toStdString()
                      << " t0=" << requestedT0
                      << " window=" << requestedWindow << "\n";
            return requestedPath == path && requestedT0 == 0.0 && requestedWindow == 4.0 ? 0 : 1;
        }
```

- [x] **Step 7: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_window_request_reports_requested_interval tests/native/test_bf_app.py::test_bf_app_smoke_ui_open_generated_csv_fixture -q
git diff --check HEAD
```

Expected: build exits 0; both tests pass; diff check exits 0.

Commit:

```bash
git add native/src/ipc/sidecar_client.h native/src/ipc/sidecar_client.cpp native/src/ui/main_window.h native/src/ui/main_window.cpp native/src/app/main.cpp tests/native/test_bf_app.py
git commit -m "feat: wire static browser window requests"
```

---

### Task 4: Latest Window Result Smoke With Real Controller

**Files:**
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: `MainWindow::setWindowRequestHandler(...)`, `MainWindow::setBrowserWindowSecondsForBrowser(double)`, and `FileOpenController::openWindow(...)`.
- Produces: smoke CLI `--smoke-ui-window-latest` proving UI-triggered window requests can apply the latest slice interval.

- [x] **Step 1: Write the failing latest-window smoke test**

Append this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_ui_window_latest_displays_requested_slice():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui-window-latest", str(sample), "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-window-latest-ok t0=0.02 t1=0.1" in result.stdout
    assert "status=CSV | 0.02-0.1 s | +/-200 uV | 2 visible channels" in result.stdout
```

- [x] **Step 2: Run the focused test and verify it fails**

Run:

```bash
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_window_latest_displays_requested_slice -q
```

Expected: FAIL because `--smoke-ui-window-latest` is unknown.

- [x] **Step 3: Add the latest-window smoke CLI**

In `native/src/app/main.cpp`, add this branch before `--smoke-ui-window-request`:

```cpp
        if (arg == "--smoke-ui-window-latest" && i + 1 < argc) {
            QApplication app(argc, argv);
            bf::I18nService i18n(repoRoot);
            QString i18nError;
            if (!i18n.loadLocale("en_US", &i18nError)) {
                std::cerr << "failed to load translations: " << i18nError.toStdString() << "\n";
                return 1;
            }
            const QString path = QString::fromLocal8Bit(argv[i + 1]);
            bf::MainWindow window(version, &i18n);
            bf::FileOpenController controller(repoRoot, version);
            int exitCode = 1;
            window.setWindowRequestHandler([&](const QString& activePath, double t0, double windowSeconds) {
                controller.openWindow(activePath, t0, windowSeconds);
            });
            QObject::connect(&controller, &bf::FileOpenController::loaded, &window, [&](const bf::FileOpenResult& result) {
                window.showFileOpenResult(result);
                if (result.waveform.t0 == 0.02 && result.waveform.t1 == 0.1) {
                    std::cout << "smoke-ui-window-latest-ok t0=" << result.waveform.t0
                              << " t1=" << result.waveform.t1
                              << " status=" << window.browserStatusText().toStdString() << "\n";
                    exitCode = 0;
                    app.quit();
                }
            });
            QObject::connect(&controller, &bf::FileOpenController::failed, &window, [&](const bf::AppError& error) {
                std::cerr << "smoke-ui-window-latest-failed code=" << error.code.toStdString() << "\n";
                exitCode = 1;
                app.quit();
            });
            controller.openWindow(path, 0.0, 0.03);
            bf::FileOpenResult result;
            result.session.ok = true;
            result.session.path = path;
            result.session.format = "CSV";
            result.session.fs = 100.0;
            result.session.nChannels = 2;
            result.session.nSamples = 1000;
            result.waveform.fs = 100.0;
            result.waveform.t0 = 0.02;
            result.waveform.t1 = 0.05;
            result.waveform.channelNames = {"ch1", "ch2"};
            result.waveform.data = {{2.0f, 3.0f, 4.0f}, {12.0f, 13.0f, 14.0f}};
            window.showFileOpenResult(result);
            window.setBrowserWindowSecondsForBrowser(4.0);
            QTimer::singleShot(5000, &app, [&]() {
                std::cerr << "smoke-ui-window-latest-failed code=timeout\n";
                exitCode = 1;
                app.quit();
            });
            app.exec();
            return exitCode;
        }
```

This smoke intentionally starts an older 0.03 s request, then triggers a newer valid 4 s request from t0=0.02 through the window handler. The short CSV fixture clamps the returned slice to 0.02-0.1 s. The existing `RequestGeneration` gate must prevent stale results from being applied.

- [x] **Step 4: Run verification and commit**

Run:

```bash
cmake --build native/build
python3 -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_ui_window_latest_displays_requested_slice tests/native/test_bf_app.py::test_bf_app_smoke_ui_open_latest_keeps_newer_result -q
git diff --check HEAD
```

Expected: build exits 0; both latest-result smokes pass; diff check exits 0.

Commit:

```bash
git add native/src/app/main.cpp tests/native/test_bf_app.py
git commit -m "feat: add latest browser window smoke"
```

---

### Task 5: M2-3 Documentation And Full Verification

**Files:**
- Modify: `docs/reports/m2-rendering-gpu-gate.md`
- Modify: `docs/feature-parity-checklist.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/superpowers/plans/2026-07-12-m2-3-gpu-browser-acceptance.md`

**Interfaces:**
- Consumes: completed Tasks 1-4.
- Produces: explicit M2-3 status, conservative M2 gate reporting, and a verified branch ready to merge.

- [x] **Step 1: Update GPU gate report**

In `docs/reports/m2-rendering-gpu-gate.md`, add this section after the current offscreen note:

```markdown
## M2-3 Status

M2-3 adds machine-readable benchmark output and browser interaction smokes. These are still development evidence unless run on a real GPU/display environment.

Current gate status: Open.
```

If a real GPU/display benchmark has actually been run during execution, add a real result row with the measured FPS and environment. If it has not been run, keep the existing `Not yet run on real GPU/display` row.

- [x] **Step 2: Update feature checklist narrowly**

In `docs/feature-parity-checklist.md`, under `## 波形显示 — M2`, keep these checked:

```markdown
- [x] 多通道波形（垂直滚动）
- [x] 通道显隐
- [x] 时间窗 1–120s（默认 10s）
- [x] 幅值缩放（默认 ±200μV）
- [x] 大窗口或高采样率数据使用 MinMax、LTTB 或等价抽稀策略
```

Do not check this item unless real GPU evidence was recorded:

```markdown
- [x] 真实 GPU/显示环境复测记录已写入 `docs/reports/m2-rendering-gpu-gate.md`
```

If Tasks 2-4 passed but real GPU was not run, leave `静态浏览和回放刷新（帧率达 M0 基线）` unchecked because the FPS baseline is not accepted yet.

- [x] **Step 3: Update roadmap current phase**

In `docs/roadmap.md`, replace the current phase paragraph with:

```markdown
当前阶段进入 M2-3：真实 GPU/显示门禁、静态浏览交互加固和 M2 验收证据收口。M2-3 仍不代表完整 M2 验收；只有 `docs/reports/m2-rendering-gpu-gate.md` 记录真实 GPU/显示环境通过后，M2 波形浏览门禁才能关闭。
```

Update `last_updated` to `2026-07-12`.

- [x] **Step 4: Run full verification**

Run these commands in order:

```bash
cmake --build native/build
python3 -m pytest tests/fixtures tests/sidecar tests/native tests/contracts tests/i18n -q
./scripts/verify_m1_foundation.sh --fast
python3 scripts/collect_m2_format_evidence.py --dry-run
git diff --check HEAD
```

Expected:

```text
native build exits 0
all non-skipped tests pass
M1 foundation verification passed
format evidence script exits 0
git diff --check exits 0
```

- [x] **Step 5: Mark plan execution checkboxes**

In `docs/superpowers/plans/2026-07-12-m2-3-gpu-browser-acceptance.md`, mark completed task steps with `[x]` only after the commands above pass. Do not mark real GPU gate acceptance complete unless real GPU/display evidence exists.

- [x] **Step 6: Commit documentation and verification status**

Commit:

```bash
git add docs/reports/m2-rendering-gpu-gate.md docs/feature-parity-checklist.md docs/roadmap.md docs/superpowers/plans/2026-07-12-m2-3-gpu-browser-acceptance.md
git commit -m "docs: record m2-3 browser acceptance status"
```

---

## Self-Review

Spec coverage:

- GPU benchmark evidence capture is covered by Task 1 and Task 5.
- Channel list visibility and vertical browsing state are covered by Task 2.
- Time-window request wiring is covered by Task 3.
- Latest-request-wins for UI-triggered window changes is covered by Task 4.
- Conservative documentation and full verification are covered by Task 5.

Placeholder scan:

- The plan contains no deferred implementation placeholders.
- Every task has exact files, test code, implementation snippets, commands, expected results, and commit commands.

Type consistency:

- `SidecarOpenResult::path` is introduced in Task 3 before `MainWindow` reads `currentFile_.session.path`.
- `MainWindow::setWindowRequestHandler`, `setBrowserWindowSecondsForBrowser`, `setFirstVisibleChannelForBrowser`, `setChannelVisibleForBrowser`, and `browserControlSummary` are defined before smoke CLIs use them.
- `WaveformViewState`, `WaveformDisplayModel`, `FileOpenController::openWindow`, and `RequestGeneration` match the interfaces already present on `master`.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-12-m2-3-gpu-browser-acceptance.md`.

Two execution options:

1. Subagent-Driven (recommended) - dispatch a fresh subagent per task, review between tasks, fast iteration.
2. Inline Execution - execute tasks in this session using executing-plans, batch execution with checkpoints.
