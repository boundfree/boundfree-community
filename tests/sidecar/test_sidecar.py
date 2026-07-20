import json
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SIDECAR_ENV = {**os.environ, "PYTHONPATH": str(ROOT / "sidecar")}


def _run_module(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "boundfree_sidecar", *args],
        cwd=ROOT,
        env=SIDECAR_ENV,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )


def test_sidecar_version_matches_version_json():
    version = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))

    result = _run_module("--version")

    assert result.stdout.strip() == (
        f'{version["product_name"]} {version["version"]} '
        f'ipc_protocol_version={version["ipc_protocol_version"]}'
    )


def test_sidecar_hello_returns_product_and_protocol_versions():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "request-1", "method": "hello", "params": {}})

    assert response == {
        "id": "request-1",
        "ok": True,
        "result": {
            "product_version": "0.1.0-dev",
            "ipc_protocol_version": 1,
        },
    }


def test_sidecar_unknown_method_returns_error_envelope():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "request-2", "method": "bad_method", "params": {}})

    assert response["id"] == "request-2"
    assert response["ok"] is False
    assert response["error"]["code"] == "protocol.unknown_method"
    assert response["error"]["params"] == {"method": "bad_method"}
    assert response["error"]["retryable"] is False
    assert response["error"]["trace_id"]


def test_sidecar_missing_field_returns_error_envelope():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "request-3", "params": {}})

    assert response["id"] == "request-3"
    assert response["ok"] is False
    assert response["error"]["code"] == "protocol.missing_field"
    assert response["error"]["params"] == {"field": "method"}
    assert response["error"]["retryable"] is False


def test_sidecar_stdio_process_handles_hello_and_unknown_method():
    env = {
        **SIDECAR_ENV,
        "PYTHONIOENCODING": "utf-8",
        "PYTHONUTF8": "1",
    }
    process = subprocess.Popen(
        [sys.executable, "-u", "-m", "boundfree_sidecar", "--stdio"],
        cwd=ROOT,
        env=env,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=1,
    )
    assert process.stdin is not None
    assert process.stdout is not None

    try:
        process.stdin.write(json.dumps({"id": "stdio-1", "method": "hello", "params": {}}) + "\n")
        process.stdin.flush()
        hello_line = process.stdout.readline()
        assert hello_line, f"sidecar produced no stdout for hello; stderr={process.stderr.read() if process.stderr else ''}"
        hello = json.loads(hello_line)
        assert hello["id"] == "stdio-1"
        assert hello["ok"] is True
        assert hello["result"]["product_version"] == "0.1.0-dev"
        assert hello["result"]["ipc_protocol_version"] == 1

        process.stdin.write(json.dumps({"id": "stdio-2", "method": "bad_method", "params": {}}) + "\n")
        process.stdin.flush()
        bad_line = process.stdout.readline()
        assert bad_line, f"sidecar produced no stdout for unknown method; stderr={process.stderr.read() if process.stderr else ''}"
        bad_method = json.loads(bad_line)
        assert bad_method["id"] == "stdio-2"
        assert bad_method["ok"] is False
        assert bad_method["error"]["code"] == "protocol.unknown_method"

        process.stdin.write(json.dumps(
            {"id": "stdio-3", "method": "未知方法", "params": {}},
            ensure_ascii=False,
        ) + "\n")
        process.stdin.flush()
        unicode_line = process.stdout.readline()
        assert unicode_line, f"sidecar produced no stdout for unicode method; stderr={process.stderr.read() if process.stderr else ''}"
        unicode_method = json.loads(unicode_line)

        assert unicode_method["id"] == "stdio-3"
        assert unicode_method["error"]["params"]["method"] == "未知方法"
    finally:
        if process.stdin is not None and not process.stdin.closed:
            process.stdin.close()
        if process.poll() is None:
            process.wait(timeout=3)
        assert process.returncode == 0


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


def test_sidecar_get_slice_rejects_boolean_number_param():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request({"id": "slice-bool", "method": "get_slice", "params": {"handle": "h1", "t0": True, "t1": 2.0}})

    assert response["id"] == "slice-bool"
    assert response["ok"] is False
    assert response["error"]["code"] == "protocol.invalid_params"
    assert response["error"]["params"] == {"field": "t0", "reason": "required number"}


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
