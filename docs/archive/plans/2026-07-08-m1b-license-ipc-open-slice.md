---
title: M1-B 授权骨架与 IPC 扩展实施计划
doc_type: plan
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-07-08
archived_at: 2026-10-23
---

# M1-B License IPC Open Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the smallest testable bridge from M1-A to M2: license contract skeleton plus sidecar `open` / `get_slice` / `close` protocol shape and error handling, without rendering or full file support yet.

**Architecture:** Keep M1-A's stdio JSON control channel. M1-B adds stable request validation, file-session error paths, a C++ generic sidecar request helper, and deterministic machine-id generation; the first successful file reader is deferred to M2.

**Tech Stack:** C++17, Qt6 Core, Python 3, pytest, JSON IPC, Qt Linguist `.ts`, `contracts/error_codes.json`.

## Global Constraints

- Work in `boundfree-community`; do not modify `../boundfree-eeg-upper-computer`.
- Keep `version.json` unchanged at `0.1.0-dev`.
- File parsing stays in the Python sidecar; C++ does not parse EEG research formats.
- Backends return `code + params`; user-visible text stays in Qt i18n files.
- EEG payload shape remains `[n_channels, n_samples]`, unit `uV`, little-endian for future binary payloads.
- M1-B must not claim real EDF/HDF5/CSV/BrainVision/EEGLAB/FIF/Intan support.
- v1 stable required languages are `zh_CN` and `en_US`; `pseudo` remains a development check locale.
- Verification commands must be run from the repository root unless a step says otherwise.

---

### Task 1: Error Contract Expansion

**Files:**
- Modify: `contracts/error_codes.json`
- Modify: `native/i18n/bf_app_zh_CN.ts`
- Modify: `native/i18n/bf_app_en_US.ts`
- Modify: `native/i18n/bf_app_pseudo.ts`
- Modify: `docs/contracts.md`
- Test: `tests/contracts/test_error_codes.py`

**Interfaces:**
- Consumes: existing `error_payload(make_error(code, params))` in `sidecar/boundfree_sidecar/errors.py` and `makeError(code, params)` in `native/src/core/app_error.cpp`.
- Produces: registered error codes used by sidecar protocol validation, file-session errors, and license skeleton.

- [x] **Step 1: Write the failing error-code test**

Add this test to `tests/contracts/test_error_codes.py`:

```python
def test_m1b_error_codes_are_registered():
    registry = json.loads(ERROR_CODES_PATH.read_text(encoding="utf-8"))
    by_code = {entry["code"]: entry for entry in registry["error_codes"]}

    expected = {
        "protocol.invalid_params": ["field", "reason"],
        "file.not_found": ["path"],
        "reader.unsupported_format": ["path", "suffix"],
        "reader.session_not_found": ["handle"],
        "reader.slice_out_of_range": ["handle", "t0", "t1", "duration"],
        "license.missing": ["path"],
        "license.invalid_signature": ["reason"],
        "license.expired": ["expires_at"],
    }
    for code, params in expected.items():
        assert code in by_code
        assert by_code[code]["params"] == params
```

- [x] **Step 2: Run the new contract test and verify it fails**

Run:

```bash
python -m pytest tests/contracts/test_error_codes.py::test_m1b_error_codes_are_registered -q
```

Expected: FAIL because `protocol.invalid_params` is not registered.

- [x] **Step 3: Add M1-B errors to `contracts/error_codes.json`**

Append these entries to the `error_codes` array after `protocol.version_mismatch` and before `ipc.sidecar_start_failed`:

```json
{
  "code": "protocol.invalid_params",
  "params": ["field", "reason"],
  "retryable": false,
  "description": "Request parameters are invalid."
},
{
  "code": "file.not_found",
  "params": ["path"],
  "retryable": false,
  "description": "Requested file does not exist."
},
{
  "code": "reader.unsupported_format",
  "params": ["path", "suffix"],
  "retryable": false,
  "description": "No sidecar reader is available for the requested file format."
},
{
  "code": "reader.session_not_found",
  "params": ["handle"],
  "retryable": false,
  "description": "Requested file session handle does not exist."
},
{
  "code": "reader.slice_out_of_range",
  "params": ["handle", "t0", "t1", "duration"],
  "retryable": false,
  "description": "Requested data slice is outside the opened file duration."
},
{
  "code": "license.missing",
  "params": ["path"],
  "retryable": false,
  "description": "Offline license file is missing."
},
{
  "code": "license.invalid_signature",
  "params": ["reason"],
  "retryable": false,
  "description": "Offline license signature verification failed."
},
{
  "code": "license.expired",
  "params": ["expires_at"],
  "retryable": false,
  "description": "Offline license is expired."
}
```

- [x] **Step 4: Add i18n messages for all new error codes**

Add matching `<message>` entries to `native/i18n/bf_app_zh_CN.ts`, `native/i18n/bf_app_en_US.ts`, and `native/i18n/bf_app_pseudo.ts`.

Use these `zh_CN` translations:

```xml
<message><source>error.protocol.invalid_params</source><translation>协议参数无效：{field}，{reason}</translation></message>
<message><source>error.file.not_found</source><translation>文件不存在：{path}</translation></message>
<message><source>error.reader.unsupported_format</source><translation>暂不支持该文件格式：{path}，后缀 {suffix}</translation></message>
<message><source>error.reader.session_not_found</source><translation>文件会话不存在：{handle}</translation></message>
<message><source>error.reader.slice_out_of_range</source><translation>切片超出文件时长：{handle}，{t0}-{t1}，时长 {duration}</translation></message>
<message><source>error.license.missing</source><translation>缺少离线授权文件：{path}</translation></message>
<message><source>error.license.invalid_signature</source><translation>离线授权签名无效：{reason}</translation></message>
<message><source>error.license.expired</source><translation>离线授权已过期：{expires_at}</translation></message>
```

Use these `en_US` translations:

```xml
<message><source>error.protocol.invalid_params</source><translation>Invalid protocol parameter: {field}, {reason}</translation></message>
<message><source>error.file.not_found</source><translation>File does not exist: {path}</translation></message>
<message><source>error.reader.unsupported_format</source><translation>Unsupported file format: {path}, suffix {suffix}</translation></message>
<message><source>error.reader.session_not_found</source><translation>File session does not exist: {handle}</translation></message>
<message><source>error.reader.slice_out_of_range</source><translation>Slice is outside file duration: {handle}, {t0}-{t1}, duration {duration}</translation></message>
<message><source>error.license.missing</source><translation>Offline license file is missing: {path}</translation></message>
<message><source>error.license.invalid_signature</source><translation>Offline license signature is invalid: {reason}</translation></message>
<message><source>error.license.expired</source><translation>Offline license expired at: {expires_at}</translation></message>
```

Use pseudo translations with the same placeholders:

```xml
<message><source>error.protocol.invalid_params</source><translation>[!! Invalid protocol parameter: {field}, {reason} !!]</translation></message>
<message><source>error.file.not_found</source><translation>[!! File does not exist: {path} !!]</translation></message>
<message><source>error.reader.unsupported_format</source><translation>[!! Unsupported file format: {path}, suffix {suffix} !!]</translation></message>
<message><source>error.reader.session_not_found</source><translation>[!! File session does not exist: {handle} !!]</translation></message>
<message><source>error.reader.slice_out_of_range</source><translation>[!! Slice is outside file duration: {handle}, {t0}-{t1}, duration {duration} !!]</translation></message>
<message><source>error.license.missing</source><translation>[!! Offline license file is missing: {path} !!]</translation></message>
<message><source>error.license.invalid_signature</source><translation>[!! Offline license signature is invalid: {reason} !!]</translation></message>
<message><source>error.license.expired</source><translation>[!! Offline license expired at: {expires_at} !!]</translation></message>
```

- [x] **Step 5: Document the new namespaces**

In `docs/contracts.md`, extend the "后续阶段按功能扩展" list so M1-B explicitly owns `protocol.invalid_params`, `file.*`, `reader.*`, and `license.*`.

- [x] **Step 6: Run contract verification**

Run:

```bash
python3 -m json.tool contracts/error_codes.json
python -m pytest tests/contracts -q
python scripts/common/check_i18n.py
```

Expected: JSON prints formatted content; pytest reports all contract tests passing; i18n check reports success.

### Task 2: Sidecar Protocol Shape

**Files:**
- Create: `sidecar/boundfree_sidecar/sessions.py`
- Modify: `sidecar/boundfree_sidecar/protocol.py`
- Test: `tests/sidecar/test_sidecar.py`

**Interfaces:**
- Consumes: `success_response(request_id, result)` and `error_response(request_id, code, params)` from `sidecar/boundfree_sidecar/protocol.py`.
- Produces: sidecar handlers for `open`, `get_slice`, and `close` with stable error envelopes.

- [x] **Step 1: Write failing protocol tests**

Append these tests to `tests/sidecar/test_sidecar.py`:

```python
def test_sidecar_open_requires_path_param():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "open-1", "method": "open", "params": {}})

    assert response["id"] == "open-1"
    assert response["ok"] is False
    assert response["error"]["code"] == "protocol.invalid_params"
    assert response["error"]["params"] == {"field": "path", "reason": "required string"}


def test_sidecar_open_missing_file_returns_file_not_found(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    missing = tmp_path / "missing.edf"
    response = handle_request({"id": "open-2", "method": "open", "params": {"path": str(missing)}})

    assert response["id"] == "open-2"
    assert response["ok"] is False
    assert response["error"]["code"] == "file.not_found"
    assert response["error"]["params"] == {"path": str(missing)}


def test_sidecar_open_existing_unsupported_file_returns_reader_error(tmp_path):
    from boundfree_sidecar.protocol import handle_request

    sample = tmp_path / "sample.xyz"
    sample.write_text("not an eeg file", encoding="utf-8")
    response = handle_request({"id": "open-3", "method": "open", "params": {"path": str(sample)}})

    assert response["id"] == "open-3"
    assert response["ok"] is False
    assert response["error"]["code"] == "reader.unsupported_format"
    assert response["error"]["params"] == {"path": str(sample), "suffix": ".xyz"}


def test_sidecar_get_slice_validates_params():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "slice-1", "method": "get_slice", "params": {"handle": "h1", "t0": 2.0, "t1": 1.0}})

    assert response["id"] == "slice-1"
    assert response["ok"] is False
    assert response["error"]["code"] == "protocol.invalid_params"
    assert response["error"]["params"] == {"field": "t1", "reason": "must be greater than t0"}


def test_sidecar_get_slice_unknown_handle_returns_reader_error():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "slice-2", "method": "get_slice", "params": {"handle": "missing", "t0": 0.0, "t1": 1.0}})

    assert response["id"] == "slice-2"
    assert response["ok"] is False
    assert response["error"]["code"] == "reader.session_not_found"
    assert response["error"]["params"] == {"handle": "missing"}


def test_sidecar_close_unknown_handle_returns_reader_error():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "close-1", "method": "close", "params": {"handle": "missing"}})

    assert response["id"] == "close-1"
    assert response["ok"] is False
    assert response["error"]["code"] == "reader.session_not_found"
    assert response["error"]["params"] == {"handle": "missing"}
```

- [x] **Step 2: Run sidecar tests and verify the new cases fail**

Run:

```bash
python -m pytest tests/sidecar/test_sidecar.py -q
```

Expected: FAIL for `open`, `get_slice`, and `close` because they still return `protocol.unknown_method`.

- [x] **Step 3: Add a minimal session registry**

Create `sidecar/boundfree_sidecar/sessions.py`:

```python
from dataclasses import dataclass
from itertools import count
from pathlib import Path


@dataclass(frozen=True)
class FileSession:
    handle: str
    path: Path
    fs: float
    n_channels: int
    n_samples: int
    duration: float
    ch_names: list[str]


class FileSessionRegistry:
    def __init__(self) -> None:
        self._next_id = count(1)
        self._sessions: dict[str, FileSession] = {}

    def add(self, path: Path, fs: float, n_channels: int, n_samples: int, ch_names: list[str]) -> FileSession:
        handle = f"file-{next(self._next_id)}"
        duration = n_samples / fs if fs > 0 else 0.0
        session = FileSession(
            handle=handle,
            path=path,
            fs=fs,
            n_channels=n_channels,
            n_samples=n_samples,
            duration=duration,
            ch_names=ch_names,
        )
        self._sessions[handle] = session
        return session

    def get(self, handle: str) -> FileSession | None:
        return self._sessions.get(handle)

    def remove(self, handle: str) -> bool:
        return self._sessions.pop(handle, None) is not None
```

- [x] **Step 4: Implement protocol validation and error paths**

Modify `sidecar/boundfree_sidecar/protocol.py` so it imports `Path` and defines a module-level registry:

```python
from pathlib import Path

from .sessions import FileSessionRegistry

_sessions = FileSessionRegistry()
```

Add helper functions above `handle_request`:

```python
def _params(request: dict[str, Any]) -> dict[str, Any]:
    params = request.get("params", {})
    return params if isinstance(params, dict) else {}


def _required_string(params: dict[str, Any], field: str) -> str | None:
    value = params.get(field)
    return value if isinstance(value, str) and value else None


def _required_number(params: dict[str, Any], field: str) -> float | None:
    value = params.get(field)
    return float(value) if isinstance(value, int | float) else None


def _handle_open(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    raw_path = _required_string(params, "path")
    if raw_path is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "path", "reason": "required string"})

    path = Path(raw_path)
    if not path.exists():
        return error_response(request_id, "file.not_found", {"path": raw_path})

    return error_response(
        request_id,
        "reader.unsupported_format",
        {"path": raw_path, "suffix": path.suffix.lower()},
    )


def _handle_get_slice(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    handle = _required_string(params, "handle")
    if handle is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "handle", "reason": "required string"})
    t0 = _required_number(params, "t0")
    if t0 is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "t0", "reason": "required number"})
    t1 = _required_number(params, "t1")
    if t1 is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "t1", "reason": "required number"})
    if t1 <= t0:
        return error_response(request_id, "protocol.invalid_params", {"field": "t1", "reason": "must be greater than t0"})

    session = _sessions.get(handle)
    if session is None:
        return error_response(request_id, "reader.session_not_found", {"handle": handle})
    if t0 < 0 or t1 > session.duration:
        return error_response(
            request_id,
            "reader.slice_out_of_range",
            {"handle": handle, "t0": t0, "t1": t1, "duration": session.duration},
        )

    return error_response(request_id, "reader.unsupported_format", {"path": str(session.path), "suffix": session.path.suffix.lower()})


def _handle_close(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    handle = _required_string(params, "handle")
    if handle is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "handle", "reason": "required string"})
    if not _sessions.remove(handle):
        return error_response(request_id, "reader.session_not_found", {"handle": handle})
    return success_response(request_id, {"closed": True})
```

Extend `handle_request`:

```python
    params = _params(request)
    if method == "open":
        return _handle_open(request_id, params)
    if method == "get_slice":
        return _handle_get_slice(request_id, params)
    if method == "close":
        return _handle_close(request_id, params)
```

- [x] **Step 5: Run sidecar verification**

Run:

```bash
python -m pytest tests/sidecar -q
```

Expected: all sidecar tests pass.

### Task 3: C++ Sidecar Request Helper

**Files:**
- Modify: `native/src/ipc/sidecar_client.h`
- Modify: `native/src/ipc/sidecar_client.cpp`
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: sidecar `open` error envelope from Task 2.
- Produces: C++ helper methods that preserve sidecar error `code`, `params`, and `retryable`.

- [x] **Step 1: Write a failing native smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_sidecar_open_missing_file_reports_file_error(tmp_path):
    missing = tmp_path / "missing.edf"

    result = _run([str(APP), "--smoke-sidecar-open", str(missing)])

    assert result.stdout.strip() == f"smoke-sidecar-open-error code=file.not_found path={missing}"
```

- [x] **Step 2: Run the native smoke test and verify it fails**

Run:

```bash
python -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_sidecar_open_missing_file_reports_file_error -q
```

Expected: FAIL because `--smoke-sidecar-open` is not handled.

- [x] **Step 3: Add response structs to `sidecar_client.h`**

Add these declarations after `SidecarHelloResult`:

```cpp
struct SidecarResponse {
    bool ok = false;
    QJsonObject result;
    AppError error;
};

struct SidecarOpenResult {
    bool ok = false;
    QString handle;
    double fs = 0.0;
    int nChannels = 0;
    double duration = 0.0;
    QStringList chNames;
    AppError error;
};
```

Add public methods:

```cpp
SidecarResponse request(const QString& method, const QJsonObject& params, int timeoutMs = 3000);
SidecarOpenResult openFile(const QString& path, int timeoutMs = 3000);
```

- [x] **Step 4: Implement generic request parsing in `sidecar_client.cpp`**

Add a helper near the anonymous namespace:

```cpp
AppError appErrorFromJson(const QJsonObject& error) {
    return makeError(
        error.value("code").toString("internal.unexpected"),
        error.value("params").toObject(),
        error.value("retryable").toBool(false));
}
```

Add the method:

```cpp
SidecarResponse SidecarClient::request(const QString& method, const QJsonObject& params, int timeoutMs) {
    SidecarResponse result;
    if (process_.state() == QProcess::NotRunning) {
        result.error = makeError("ipc.sidecar_exited", {{"exit_code", process_.exitCode()}});
        return result;
    }

    const QJsonObject request{{"id", method + "-1"}, {"method", method}, {"params", params}};
    if (!writeJsonLine(request, timeoutMs)) {
        result.error = makeError("ipc.sidecar_timeout", {{"operation", method + "_write"}, {"timeout_ms", timeoutMs}});
        return result;
    }

    const QByteArray line = readJsonLine(timeoutMs);
    if (line.isEmpty()) {
        result.error = makeError("ipc.sidecar_timeout", {{"operation", method + "_read"}, {"timeout_ms", timeoutMs}});
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        result.error = makeError("protocol.invalid_json");
        return result;
    }

    const QJsonObject response = doc.object();
    if (!response.value("ok").toBool(false)) {
        result.error = appErrorFromJson(response.value("error").toObject());
        return result;
    }

    result.ok = true;
    result.result = response.value("result").toObject();
    return result;
}
```

Add `openFile`:

```cpp
SidecarOpenResult SidecarClient::openFile(const QString& path, int timeoutMs) {
    SidecarOpenResult result;
    const SidecarResponse response = request("open", {{"path", path}}, timeoutMs);
    if (!response.ok) {
        result.error = response.error;
        return result;
    }

    const QJsonObject payload = response.result;
    result.handle = payload.value("handle").toString();
    result.fs = payload.value("fs").toDouble();
    result.nChannels = payload.value("n_channels").toInt();
    result.duration = payload.value("duration").toDouble();
    const QJsonArray channels = payload.value("ch_names").toArray();
    for (const QJsonValue& value : channels) {
        result.chNames.push_back(value.toString());
    }
    result.ok = true;
    return result;
}
```

Refactor `startAndHello` to use `request("hello", {})` after the process starts, preserving the existing public behavior and `protocol.version_mismatch` check.

- [x] **Step 5: Add the smoke CLI option**

In `native/src/app/main.cpp`, add an early argument branch before creating `QApplication`:

```cpp
if (arg == "--smoke-sidecar-open" && i + 1 < argc) {
    QCoreApplication app(argc, argv);
    bf::SidecarClient sidecar(repoRoot);
    const bf::SidecarHelloResult hello = sidecar.startAndHello(version);
    if (!hello.ok) {
        std::cerr << "smoke-sidecar-open-failed code=" << hello.error.code.toStdString() << "\n";
        return 1;
    }
    const QString path = QString::fromLocal8Bit(argv[i + 1]);
    const bf::SidecarOpenResult opened = sidecar.openFile(path);
    sidecar.stop();
    if (!opened.ok) {
        std::cout << "smoke-sidecar-open-error code=" << opened.error.code.toStdString()
                  << " path=" << opened.error.params.value("path").toString().toStdString() << "\n";
        return 0;
    }
    std::cout << "smoke-sidecar-open-ok handle=" << opened.handle.toStdString()
              << " fs=" << opened.fs << " n_channels=" << opened.nChannels << "\n";
    return 0;
}
```

Also register `smoke-sidecar-open` in `QCommandLineParser` for help output.

- [x] **Step 6: Run native verification**

Run:

```bash
cmake --build native/build
python -m pytest tests/native -q
```

Expected: native build succeeds; all native tests pass.

### Task 4: License Machine ID Skeleton

**Files:**
- Create: `native/src/license/machine_id.h`
- Create: `native/src/license/machine_id.cpp`
- Modify: `native/CMakeLists.txt`
- Modify: `native/src/app/main.cpp`
- Test: `tests/native/test_bf_app.py`

**Interfaces:**
- Consumes: Qt6 Core `QCryptographicHash`.
- Produces: deterministic machine-id formatting for later RSA-2048 PSS offline license validation.

- [x] **Step 1: Write a failing machine-id smoke test**

Add this test to `tests/native/test_bf_app.py`:

```python
def test_bf_app_smoke_machine_id_from_fixed_inputs():
    result = _run(
        [
            str(APP),
            "--smoke-machine-id",
            "--machine-guid",
            "GUID-001",
            "--mac",
            "AA:BB:CC:DD:EE:FF",
        ]
    )

    assert result.stdout.strip() == "smoke-machine-id 53C42CBF-B2AE90CE-65593CDA-87F80560"
```

- [x] **Step 2: Run the machine-id test and verify it fails**

Run:

```bash
python -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_machine_id_from_fixed_inputs -q
```

Expected: FAIL because `--smoke-machine-id` is not handled.

- [x] **Step 3: Add machine-id API**

Create `native/src/license/machine_id.h`:

```cpp
#pragma once

#include <QString>

namespace bf {

QString machineIdFromInputs(const QString& machineGuid, const QString& macAddress);

}  // namespace bf
```

Create `native/src/license/machine_id.cpp`:

```cpp
#include "license/machine_id.h"

#include <QCryptographicHash>

namespace bf {

QString machineIdFromInputs(const QString& machineGuid, const QString& macAddress) {
    const QByteArray seed = (machineGuid + macAddress).toUtf8();
    const QByteArray digest = QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex().toUpper();
    const QString first32 = QString::fromLatin1(digest.left(32));
    QStringList groups;
    for (int i = 0; i < first32.size(); i += 8) {
        groups.push_back(first32.mid(i, 8));
    }
    return groups.join("-");
}

}  // namespace bf
```

- [x] **Step 4: Add files to CMake**

Add to `add_executable(bf_app ...)` in `native/CMakeLists.txt`:

```cmake
    src/license/machine_id.cpp
    src/license/machine_id.h
```

- [x] **Step 5: Add smoke CLI**

In `native/src/app/main.cpp`, include:

```cpp
#include "license/machine_id.h"
```

Add an early argument branch:

```cpp
if (arg == "--smoke-machine-id" && i + 4 < argc) {
    QCoreApplication app(argc, argv);
    QString machineGuid;
    QString mac;
    for (int j = i + 1; j + 1 < argc; j += 2) {
        const QString key = QString::fromLocal8Bit(argv[j]);
        const QString value = QString::fromLocal8Bit(argv[j + 1]);
        if (key == "--machine-guid") {
            machineGuid = value;
        } else if (key == "--mac") {
            mac = value;
        }
    }
    std::cout << "smoke-machine-id " << bf::machineIdFromInputs(machineGuid, mac).toStdString() << "\n";
    return 0;
}
```

- [x] **Step 6: Run license skeleton verification**

Run:

```bash
cmake --build native/build
python -m pytest tests/native/test_bf_app.py::test_bf_app_smoke_machine_id_from_fixed_inputs -q
```

Expected: build succeeds; machine-id smoke test passes.

### Task 5: M1-B Full Verification

**Files:**
- Modify: `docs/superpowers/plans/2026-07-08-m1b-license-ipc-open-slice.md`

**Interfaces:**
- Consumes: completed Tasks 1-4.
- Produces: verified M1-B bridge ready for M2 file-reader work.

- [x] **Step 1: Run required verification commands**

Run:

```bash
python3 -m json.tool contracts/error_codes.json
python -m pytest tests/sidecar tests/contracts -q
cmake --build native/build
native/build/bf_app --version
```

Expected:

```text
contracts/error_codes.json prints valid JSON
sidecar and contract tests pass
native build exits 0
BoundFree EEG 0.1.0-dev
```

- [x] **Step 2: Run full M1 foundation verification**

Run:

```bash
./scripts/wsl/verify_m1_foundation.sh --fast
```

Expected: `M1 foundation verification passed`.

- [x] **Step 3: Review diff**

Run:

```bash
git diff --check HEAD
git diff --stat
```

Expected: no whitespace errors; diff contains only M1-B code, tests, docs, and plan updates.
