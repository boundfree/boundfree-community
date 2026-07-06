from dataclasses import dataclass
from datetime import datetime
from itertools import count
from typing import Any


ERROR_CODES: dict[str, bool] = {
    "protocol.invalid_json": False,
    "protocol.missing_field": False,
    "protocol.unknown_method": False,
    "protocol.version_mismatch": False,
    "ipc.sidecar_start_failed": True,
    "ipc.sidecar_timeout": True,
    "ipc.sidecar_exited": True,
    "sidecar.initialization_failed": True,
    "internal.unexpected": False,
}

_trace_counter = count(1)


@dataclass(frozen=True)
class SidecarError:
    code: str
    params: dict[str, Any]
    retryable: bool
    trace_id: str


def make_error(code: str, params: dict[str, Any] | None = None) -> SidecarError:
    retryable = ERROR_CODES.get(code, False)
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
