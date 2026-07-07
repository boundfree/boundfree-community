from typing import Any
from pathlib import Path

from .errors import error_payload, make_error
from .readers.registry import reader_for_path
from .sessions import FileSessionRegistry
from .version import load_version

_sessions = FileSessionRegistry()


def success_response(request_id: Any, result: dict[str, Any]) -> dict[str, Any]:
    return {"id": request_id, "ok": True, "result": result}


def error_response(request_id: Any, code: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    return {"id": request_id, "ok": False, "error": error_payload(make_error(code, params))}


def _params(request: dict[str, Any]) -> dict[str, Any]:
    params = request.get("params", {})
    return params if isinstance(params, dict) else {}


def _required_string(params: dict[str, Any], field: str) -> str | None:
    value = params.get(field)
    return value if isinstance(value, str) and value else None


def _required_number(params: dict[str, Any], field: str) -> float | None:
    value = params.get(field)
    return float(value) if isinstance(value, (int, float)) else None


def _handle_open(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    raw_path = _required_string(params, "path")
    if raw_path is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "path", "reason": "required string"})

    path = Path(raw_path)
    if not path.exists():
        return error_response(request_id, "file.not_found", {"path": raw_path})

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


def _handle_close(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    handle = _required_string(params, "handle")
    if handle is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "handle", "reason": "required string"})
    if not _sessions.remove(handle):
        return error_response(request_id, "reader.session_not_found", {"handle": handle})
    return success_response(request_id, {"closed": True})


def handle_request(request: Any) -> dict[str, Any]:
    if not isinstance(request, dict):
        return error_response(None, "protocol.invalid_json")

    request_id = request.get("id")
    if "method" not in request:
        return error_response(request_id, "protocol.missing_field", {"field": "method"})

    method = request["method"]
    if method == "hello":
        version = load_version()
        return success_response(
            request_id,
            {
                "product_version": version.version,
                "ipc_protocol_version": version.ipc_protocol_version,
            },
        )

    params = _params(request)
    if method == "open":
        return _handle_open(request_id, params)
    if method == "get_slice":
        return _handle_get_slice(request_id, params)
    if method == "close":
        return _handle_close(request_id, params)

    return error_response(request_id, "protocol.unknown_method", {"method": str(method)})
