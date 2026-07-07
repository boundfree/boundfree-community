---
title: v1 需求对齐实施计划
doc_type: plan
status: active
owner: BoundFree
last_updated: 2026-07-08
---

# V1 Requirements Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Align `boundfree-eeg-native` with the root v1 requirements: v1 stable is a general EEG/electrophysiology file analysis product, while self-developed hardware acquisition moves to v1.1/v2.

**Architecture:** Keep the existing C++/Qt6 native main app plus Python codec sidecar architecture. The immediate implementation is a documentation and roadmap alignment pass that makes the native repository's active docs consistent with `../BoundFree_EEG_System_Requirements_Spec.md`; later code plans should be derived from the aligned roadmap and parity checklist.

**Tech Stack:** Markdown docs, C++17/Qt6 native app, Python sidecar, JSON IPC, Qt Linguist i18n, Eigen and pocketfft for future DSP.

## Global Constraints

- Communicate with the user in Simplified Chinese.
- Do not modify `../boundfree-eeg-upper-computer` unless the user explicitly asks.
- Do not revert unrelated user changes; current untracked `docs/style-preview.html` must be left untouched.
- v1 stable formal implementation is `boundfree-eeg-native`.
- v1 stable product boundary is general EEG/electrophysiology file analysis for animal electrophysiology research.
- v1 stable does not promise the self-developed `ADS1299-4 + ESP32-C3` hardware acquisition loop.
- v1.1/v2 owns BLE acquisition, real device recording, device status monitoring, and 12-hour acquisition stability tests.
- v1 must retain data source, Session, recording, and realtime pipeline extension boundaries.
- v1 required languages are `zh_CN` and `en_US`; other locale files may remain for architecture/key integrity but are not v1 acceptance targets.
- v1 published platform is Windows 10/11.
- Formal release packages must not contain keygen, private keys, build caches, temporary files, or sensitive materials.

---

### Task 1: Native Documentation Alignment

**Files:**
- Modify: `README.md`
- Modify: `docs/roadmap.md`
- Modify: `docs/feature-parity-checklist.md`
- Modify: `docs/architecture-design.md`
- Modify: `docs/contracts.md`
- Modify: `docs/README.md`

**Interfaces:**
- Consumes: root requirements from `../BoundFree_EEG_System_Requirements_Spec.md` and `../BoundFree_EEG_v1_requirements_rewrite_plan.md`.
- Produces: native active docs that clearly define v1 stable, v1 target-with-sample-gate, and v1.1/v2 boundaries.

- [x] **Step 1: Update roadmap product boundary**

Edit `docs/roadmap.md` so the opening sections state that v1 stable is general file analysis and hardware acquisition is v1.1/v2. Keep M1-A as the current stage.

- [x] **Step 2: Update feature parity checklist**

Edit `docs/feature-parity-checklist.md` to split acceptance into:

```text
v1 required parity
v1 target formats with real-sample gate
v1.1/v2 hardware acquisition
```

Remove `ADS1299` configuration and BLE source from v1 required checklist items, while retaining data source abstraction as an architecture requirement.

- [x] **Step 3: Update architecture design**

Edit `docs/architecture-design.md` so realtime and recording are described as v1 extension boundaries and simulated/file-source verifiable skeletons. State that real BLE hardware acquisition is a later v1.1/v2 implementation.

- [x] **Step 4: Update i18n contract**

Edit `docs/contracts.md` so `zh_CN`, `en_US`, and `pseudo` remain actively verified in M1-A, while other locale files are retained for key integrity only and are not v1 stable acceptance targets.

- [x] **Step 5: Update README and docs index**

Edit `README.md` and `docs/README.md` to point readers to the aligned v1 boundary and to avoid implying that self-developed hardware acquisition is a v1 release gate.

- [x] **Step 6: Run documentation verification**

Run from `boundfree-eeg-native`:

```bash
git diff --check HEAD
```

Expected: no whitespace errors.

- [x] **Step 7: Review diff**

Run from `boundfree-eeg-native`:

```bash
git diff -- README.md docs/roadmap.md docs/feature-parity-checklist.md docs/architecture-design.md docs/contracts.md docs/README.md
```

Expected: the diff only changes active docs and keeps `docs/style-preview.html` untouched.

### Task 2: M1-B Scope Plan

**Files:**
- Create: `docs/superpowers/plans/2026-07-08-m1b-license-ipc-open-slice.md`

**Interfaces:**
- Consumes: aligned `docs/roadmap.md`, `docs/contracts.md`, and `docs/feature-parity-checklist.md`.
- Produces: a detailed implementation plan for M1-B: offline license skeleton, `open/get_slice/close` protocol error paths, and sidecar contract tests.

- [x] **Step 1: Define M1-B deliverable**

Write a plan whose goal is:

```text
Build the smallest testable bridge from M1-A to M2: license contract skeleton plus sidecar open/get_slice/close protocol shape and error handling, without rendering or full file support yet.
```

- [x] **Step 2: Include required files and tests**

The M1-B plan must name exact files under:

```text
native/src/license/
native/src/ipc/
sidecar/boundfree_sidecar/
tests/sidecar/
tests/native/
contracts/error_codes.json
docs/contracts.md
```

- [x] **Step 3: Include verification commands**

The plan must include:

```bash
python3 -m json.tool contracts/error_codes.json
python -m pytest tests/sidecar tests/contracts
cmake --build native/build
native/build/bf_app --version
```

### Task 3: M2 File Analysis Entry Plan

**Files:**
- Create: `docs/superpowers/plans/2026-07-08-m2-open-file-static-waveform.md`

**Interfaces:**
- Consumes: M1-B protocol plan and v1 required file format list.
- Produces: a detailed implementation plan for file open, metadata lazy loading, slice retrieval, and static waveform browsing.

- [x] **Step 1: Define sample-gated format groups**

The plan must separate:

```text
v1 required parity: EDF/EDF+, BDF, HDF5, CSV, BrainVision, EEGLAB, MNE FIF, Intan RHS, BBK/DAT/LSD
v1 target with real-sample gate: Intan RHD/RHS, Open Ephys, Plexon, Blackrock
```

- [x] **Step 2: Define first testable path**

The first implementation slice must open one simple sample or generated fixture through the sidecar, return metadata only, then fetch a visible time-window slice.

- [x] **Step 3: Define rendering gate**

The plan must require a real GPU/display recheck before M2 waveform acceptance and must not treat WSL/Xvfb/Mesa as final rendering acceptance.

### Task 4: Post-Plan Execution Handoff

**Files:**
- Modify only the plan files from Tasks 1-3 unless the user chooses immediate implementation.

**Interfaces:**
- Consumes: completed plan documents.
- Produces: user-visible execution choice and a recommended first implementation path.

- [x] **Step 1: Summarize current repository status**

Report:

```text
branch: docs/v1-requirements-alignment
base branch: master
current version: 0.1.0-dev
current stage: M1-A skeleton exists
untracked file left untouched: docs/style-preview.html
```

- [x] **Step 2: Recommend execution order**

Recommend:

```text
1. Execute Task 1 documentation alignment now.
2. Review the aligned roadmap/checklist.
3. Execute M1-B.
4. Execute M2 in small sample-gated slices.
```

- [x] **Step 3: Ask for execution mode only if needed**

If implementation will be delegated or batched, ask the user to choose subagent-driven execution or inline execution. If the user has already requested immediate execution, proceed inline with Task 1 first.
