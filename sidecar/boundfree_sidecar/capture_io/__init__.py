"""Optional capture.v1 capability; no IPC or format dependency changes to older callers."""

from .model import CaptureError

_registry = None


def available():
    try:
        import h5py  # noqa: F401 - optional native modules may be present but unloadable
        import numpy  # noqa: F401
    except Exception:
        return False
    return True


def handle_capture(method, params, payload=None):
    global _registry
    if not available():
        raise CaptureError("capture.unavailable", {"dependency": "numpy/h5py"})
    if method == "capture_inspect":
        from .inspect import inspect
        return inspect(params)
    if _registry is None:
        from .session import CaptureRegistry
        _registry = CaptureRegistry()
    if method == "capture_append":
        return _registry.append(params, payload)
    return {"capture_open": _registry.open, "capture_event": _registry.event,
            "capture_finalize": _registry.finalize}[method](params)


def cleanup():
    if _registry is not None:
        _registry.cleanup()
