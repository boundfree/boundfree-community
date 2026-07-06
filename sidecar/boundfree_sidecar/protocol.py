from typing import Any

from .errors import error_payload, make_error
from .version import load_version


def success_response(request_id: Any, result: dict[str, Any]) -> dict[str, Any]:
    return {"id": request_id, "ok": True, "result": result}


def error_response(request_id: Any, code: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    return {"id": request_id, "ok": False, "error": error_payload(make_error(code, params))}


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

    return error_response(request_id, "protocol.unknown_method", {"method": str(method)})
