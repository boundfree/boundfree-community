import json
from dataclasses import dataclass
from datetime import datetime
from itertools import count
from pathlib import Path
from typing import Any


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def load_error_retryability() -> dict[str, bool]:
    registry_path = _repo_root() / "contracts" / "error_codes.json"
    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    return {
        str(entry["code"]): bool(entry["retryable"])
        for entry in registry["error_codes"]
    }

_trace_counter = count(1)


@dataclass(frozen=True)
class SidecarError:
    code: str
    params: dict[str, Any]
    retryable: bool
    trace_id: str


def make_error(code: str, params: dict[str, Any] | None = None) -> SidecarError:
    retryable = load_error_retryability().get(code, False)
    stamp = datetime.utcnow().strftime("%Y%m%d")
    trace_id = f"{stamp}-{next(_trace_counter):06d}"
    return SidecarError(code=code, params=params or {}, retryable=retryable, trace_id=trace_id)


def error_payload(error: SidecarError) -> dict[str, Any]:
    return {
        "code": error.code,
        "params": error.params,
        "retryable": error.retryable,
        "trace_id": error.trace_id,
    }
