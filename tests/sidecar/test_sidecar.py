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
