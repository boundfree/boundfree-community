import csv
import math
from pathlib import Path
from typing import Any

from .errors import error_payload, make_error
from .analysis_io import AnalysisError, handle_analysis
from .capture_io import CaptureError, available as capture_available, handle_capture
from .readers.errors import CsvSchemaError, CaptureReaderError, OptionalDependencyMissing
from .readers.registry import reader_for_path
from .sessions import FileSessionRegistry
from .version import load_version
from .writers.export import export_segment, export_samples
from .writers.recording import RecordingRegistry

_sessions = FileSessionRegistry()
_recordings = RecordingRegistry()


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
    return float(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def _required_positive_int(params: dict[str, Any], field: str, default: int | None = None) -> int | None:
    value = params.get(field, default)
    return value if isinstance(value, int) and not isinstance(value, bool) and value > 0 else None


def _markers(params: dict[str, Any]) -> list[dict[str, Any]] | None:
    raw_markers = params.get("markers", [])
    if not isinstance(raw_markers, list):
        return None
    markers: list[dict[str, Any]] = []
    for marker in raw_markers:
        if not isinstance(marker, dict):
            return None
        time = _required_number(marker, "time")
        label = marker.get("label")
        if time is None or not math.isfinite(time) or time < 0.0 or not isinstance(label, str):
            return None
        markers.append({"time": time, "label": label})
    return markers


def _handle_open(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    raw_path = _required_string(params, "path")
    if raw_path is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "path", "reason": "required string"})
    path = Path(raw_path)
    if not path.exists():
        return error_response(request_id, "file.not_found", {"path": raw_path})
    stream = params.get("capture_stream", "")
    if not isinstance(stream, str):
        return error_response(request_id, "protocol.invalid_params", {"field": "capture_stream", "reason": "expected string"})
    reader = reader_for_path(path, stream) if stream else reader_for_path(path)
    if reader is None:
        return error_response(request_id, "reader.unsupported_format", {"path": raw_path, "suffix": path.suffix.lower()})
    try:
        meta = reader.metadata()
    except OptionalDependencyMissing as exc:
        return error_response(request_id, "reader.optional_dependency_missing", {
            "path": raw_path,
            "dependency": exc.dependency,
            "reason": exc.reason or str(exc),
        })
    except (CsvSchemaError, CaptureReaderError) as exc:
        return error_response(request_id, exc.code, {"path": raw_path, **exc.params})
    except Exception as exc:
        return error_response(request_id, "reader.open_failed", {"path": raw_path, "reason": str(exc)})
    session = _sessions.add(path, meta.fs, meta.n_channels, meta.n_samples, meta.ch_names, reader, duration=meta.duration)
    result = {
        "handle": session.handle,
        "fs": meta.fs,
        "n_channels": meta.n_channels,
        "n_samples": meta.n_samples,
        "duration": meta.duration,
        "ch_names": meta.ch_names,
        "unit": meta.unit,
        "format": meta.format_name,
        "events": meta.events,
    }
    if meta.channel_units is not None: result["channel_units"] = meta.channel_units
    if meta.capture is not None: result["capture"] = meta.capture
    if meta.source_metadata is not None: result["signal_metadata"] = meta.source_metadata
    return success_response(request_id, result)


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
    if not math.isfinite(t0) or not math.isfinite(t1) or t1 <= t0:
        return error_response(request_id, "protocol.invalid_params", {"field": "t1", "reason": "must be greater than t0"})
    session = _sessions.get(handle)
    if session is None:
        return error_response(request_id, "reader.session_not_found", {"handle": handle})
    if t0 < 0 or t1 > session.duration:
        return error_response(request_id, "reader.slice_out_of_range", {"handle": handle, "t0": t0, "t1": t1, "duration": session.duration})
    try:
        data = session.reader.get_slice(t0, t1)
    except OptionalDependencyMissing as exc:
        return error_response(request_id, "reader.optional_dependency_missing", {
            "path": str(session.path),
            "dependency": exc.dependency,
            "reason": exc.reason or str(exc),
        })
    except (CsvSchemaError, CaptureReaderError) as exc:
        return error_response(request_id, exc.code, {"path": str(session.path), **exc.params})
    except Exception as exc:
        return error_response(request_id, "reader.open_failed", {"path": str(session.path), "reason": str(exc)})
    result = {
        "handle": handle,
        "t0": data.t0,
        "t1": data.t1,
        "fs": data.fs,
        "shape": data.shape,
        "unit": data.unit,
        "data": data.data,
    }
    if data.channel_units is not None: result["channel_units"] = data.channel_units
    if data.sample_times is not None: result["sample_times"] = data.sample_times
    return success_response(request_id, result)


def _handle_close(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    handle = _required_string(params, "handle")
    if handle is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "handle", "reason": "required string"})
    if not _sessions.remove(handle):
        return error_response(request_id, "reader.session_not_found", {"handle": handle})
    return success_response(request_id, {"closed": True})


def _handle_export_markers_csv(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    raw_path = _required_string(params, "path")
    if raw_path is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "path", "reason": "required string"})
    raw_markers = params.get("markers")
    if not isinstance(raw_markers, list):
        return error_response(request_id, "protocol.invalid_params", {"field": "markers", "reason": "required array"})
    markers: list[dict[str, Any]] = []
    for index, marker in enumerate(raw_markers):
        if not isinstance(marker, dict):
            return error_response(request_id, "protocol.invalid_params", {"field": f"markers[{index}]", "reason": "required object"})
        time = _required_number(marker, "time")
        label = marker.get("label")
        if time is None or not math.isfinite(time) or time < 0.0 or not isinstance(label, str):
            return error_response(request_id, "protocol.invalid_params", {"field": f"markers[{index}]", "reason": "required non-negative time and label"})
        markers.append({"time": time, "label": label})
    markers_path = Path(raw_path).with_name(Path(raw_path).stem + "_markers.csv")
    try:
        with markers_path.open("w", encoding="utf-8", newline="") as output:
            writer = csv.writer(output)
            writer.writerow(["time_s", "label"])
            writer.writerows((marker["time"], marker["label"]) for marker in markers)
    except OSError as exc:
        return error_response(request_id, "reader.open_failed", {"path": str(markers_path), "reason": str(exc)})
    return success_response(request_id, {"path": str(markers_path), "marker_count": len(markers)})


def _handle_record_open(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    fs = _required_number(params, "fs")
    output_dir = _required_string(params, "out_dir")
    ch_names = params.get("ch_names")
    meta = params.get("meta", {})
    raw_formats = params.get("formats", ["edf", "hdf5"])
    max_queue_chunks = _required_positive_int(params, "max_queue_chunks", 256)
    if fs is None or not math.isfinite(fs) or fs <= 0.0:
        return error_response(request_id, "protocol.invalid_params", {"field": "fs", "reason": "required positive number"})
    if not isinstance(ch_names, list) or not ch_names or not all(isinstance(name, str) and name for name in ch_names):
        return error_response(request_id, "protocol.invalid_params", {"field": "ch_names", "reason": "required non-empty string array"})
    if output_dir is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "out_dir", "reason": "required string"})
    if not isinstance(meta, dict) or not all(isinstance(key, str) and isinstance(value, str) for key, value in meta.items()):
        return error_response(request_id, "protocol.invalid_params", {"field": "meta", "reason": "required string object"})
    if not isinstance(raw_formats, list) or not raw_formats or not all(value in ("edf", "hdf5") for value in raw_formats):
        return error_response(request_id, "protocol.invalid_params", {"field": "formats", "reason": "supported values are edf and hdf5"})
    if max_queue_chunks is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "max_queue_chunks", "reason": "required positive integer"})
    try:
        session = _recordings.open(fs, list(ch_names), Path(output_dir), dict(meta), tuple(dict.fromkeys(raw_formats)), max_queue_chunks)
    except Exception as exc:
        return error_response(request_id, "record.open_failed", {"path": output_dir, "reason": str(exc)})
    return success_response(request_id, {"handle": session.handle, "paths": {key: str(value) for key, value in session.paths.items()}})


def _recording_for(request_id: Any, params: dict[str, Any]):
    handle = _required_string(params, "handle")
    if handle is None:
        return None, error_response(request_id, "protocol.invalid_params", {"field": "handle", "reason": "required string"})
    session = _recordings.get(handle)
    if session is None:
        return None, error_response(request_id, "record.session_not_found", {"handle": handle})
    return session, None


def _stats_result(session) -> dict[str, Any]:
    stats = session.stats()
    return {
        "backlog_chunks": stats.backlog_chunks,
        "samples_written": stats.samples_written,
        "dropped_chunks": stats.dropped_chunks,
        "dropped_events": stats.dropped_events,
    }


def _handle_record_write(request_id: Any, params: dict[str, Any], binary_payload: bytes | None) -> dict[str, Any]:
    session, failure = _recording_for(request_id, params)
    if failure is not None:
        return failure
    shape = params.get("shape")
    if not isinstance(shape, list) or len(shape) != 2 or not all(isinstance(value, int) and not isinstance(value, bool) and value > 0 for value in shape):
        return error_response(request_id, "record.invalid_chunk", {"reason": "shape must contain two positive integers"})
    if binary_payload is None:
        return error_response(request_id, "record.invalid_chunk", {"reason": "binary payload is required"})
    try:
        accepted = session.write(binary_payload, (shape[0], shape[1]))
    except ValueError as exc:
        return error_response(request_id, "record.invalid_chunk", {"reason": str(exc)})
    except Exception as exc:
        return error_response(request_id, "record.writer_failed", {"handle": session.handle, "reason": str(exc)})
    return success_response(request_id, {"accepted": accepted, **_stats_result(session)})


def _handle_record_event(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    session, failure = _recording_for(request_id, params)
    if failure is not None:
        return failure
    time = _required_number(params, "time")
    label = params.get("label")
    if time is None or not math.isfinite(time) or time < 0.0 or not isinstance(label, str):
        return error_response(request_id, "protocol.invalid_params", {"field": "event", "reason": "required non-negative time and label"})
    try:
        accepted = session.add_event(time, label)
    except Exception as exc:
        return error_response(request_id, "record.writer_failed", {"handle": session.handle, "reason": str(exc)})
    return success_response(request_id, {"accepted": accepted, **_stats_result(session)})


def _handle_record_close(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    handle = _required_string(params, "handle")
    if handle is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "handle", "reason": "required string"})
    if _recordings.get(handle) is None:
        return error_response(request_id, "record.session_not_found", {"handle": handle})
    try:
        closed = _recordings.close(handle)
    except TimeoutError:
        return error_response(request_id, "record.close_timeout", {"handle": handle, "timeout_ms": 10000})
    except Exception as exc:
        return error_response(request_id, "record.writer_failed", {"handle": handle, "reason": str(exc)})
    assert closed is not None
    session, stats = closed
    return success_response(request_id, {
        "paths": {key: str(value) for key, value in session.paths.items()},
        "samples_written": stats.samples_written,
        "dropped_chunks": stats.dropped_chunks,
        "dropped_events": stats.dropped_events,
    })


def _handle_export_segment(request_id: Any, params: dict[str, Any]) -> dict[str, Any]:
    source = _required_string(params, "src")
    destination = _required_string(params, "path")
    t0 = _required_number(params, "t0")
    t1 = _required_number(params, "t1")
    markers = _markers(params)
    if source is None or destination is None:
        field = "src" if source is None else "path"
        return error_response(request_id, "protocol.invalid_params", {"field": field, "reason": "required string"})
    if t0 is None or t1 is None or not math.isfinite(t0) or not math.isfinite(t1) or t0 < 0.0 or t1 <= t0:
        return error_response(request_id, "export.invalid_range", {"t0": t0, "t1": t1, "duration": None})
    if markers is None:
        return error_response(request_id, "protocol.invalid_params", {"field": "markers", "reason": "required marker array"})
    if Path(destination).suffix.lower() not in (".edf", ".h5", ".hdf5", ".csv"):
        return error_response(request_id, "export.unsupported_format", {"path": destination, "suffix": Path(destination).suffix.lower()})
    if not Path(source).exists():
        return error_response(request_id, "file.not_found", {"path": source})
    try:
        return success_response(request_id, export_segment(Path(source), t0, t1, Path(destination), markers))
    except ValueError as exc:
        if "range" in str(exc):
            return error_response(request_id, "export.invalid_range", {"t0": t0, "t1": t1, "duration": None})
        return error_response(request_id, "export.failed", {"path": destination, "reason": str(exc)})
    except Exception as exc:
        return error_response(request_id, "export.failed", {"path": destination, "reason": str(exc)})


def _handle_export_samples(request_id: Any, params: dict[str, Any], binary_payload: bytes | None) -> dict[str, Any]:
    destination = _required_string(params, "path")
    fs = _required_number(params, "fs")
    t0 = _required_number(params, "t0")
    t1 = _required_number(params, "t1")
    names = params.get("ch_names")
    shape = params.get("shape")
    markers = _markers(params)
    for field, valid in (
        ("path", destination is not None),
        ("fs", fs is not None and math.isfinite(fs) and fs > 0),
        ("ch_names", isinstance(names, list) and bool(names) and all(isinstance(name, str) and name for name in names)),
        ("markers", markers is not None),
    ):
        if not valid:
            return error_response(request_id, "protocol.invalid_params", {"field": field, "reason": "invalid export metadata"})
    if t0 is None or t1 is None or not math.isfinite(t0) or not math.isfinite(t1) or not 0 <= t0 < t1:
        return error_response(request_id, "export.invalid_range", {"t0": t0, "t1": t1, "duration": None})
    if Path(destination).suffix.lower() not in (".edf", ".h5", ".hdf5", ".csv"):
        return error_response(request_id, "export.unsupported_format", {"path": destination, "suffix": Path(destination).suffix.lower()})
    if (
        not isinstance(shape, list) or len(shape) != 2
        or not all(isinstance(value, int) and not isinstance(value, bool) and value > 0 for value in shape)
        or shape[0] != len(names) or params.get("dtype") != "float32-le" or not isinstance(params.get("unit"), str) or not params.get("unit")
        or binary_payload is None or len(binary_payload) != shape[0] * shape[1] * 4
    ):
        return error_response(request_id, "ipc.invalid_frame", {"reason": "export payload metadata does not match bytes"})
    try:
        import numpy as np

        data = np.frombuffer(binary_payload, dtype="<f4").reshape(shape)
        return success_response(request_id, export_samples(data, fs, names, t0, t1, Path(destination), markers,
            unit=params.get("unit", "uV"), channel_units=params.get("channel_units"), sample_times=params.get("sample_times"), provenance=params.get("provenance")))
    except Exception as exc:
        return error_response(request_id, "export.failed", {"path": destination, "reason": str(exc)})


def handle_request(request: Any, binary_payload: bytes | None = None) -> dict[str, Any]:
    if not isinstance(request, dict):
        return error_response(None, "protocol.invalid_json")
    request_id = request.get("id")
    if "method" not in request:
        return error_response(request_id, "protocol.missing_field", {"field": "method"})
    method = request["method"]
    if method == "hello":
        version = load_version()
        result = {"product_version": version.version, "ipc_protocol_version": version.ipc_protocol_version,
                  "analysis_capabilities": ["source-f64-v1", "bundle-f64-v1"]}
        requested = _params(request).get("requested_capabilities", [])
        capture_capabilities = ("capture.v1", "capture.metadata.v1")
        if isinstance(requested, list) and any(capability in requested for capability in capture_capabilities):
            result["capabilities"] = [capability for capability in capture_capabilities if capability in requested] if capture_available() else []
        return success_response(request_id, result)
    params = _params(request)
    if method in ("capture_open", "capture_append", "capture_event", "capture_finalize", "capture_inspect"):
        try:
            return success_response(request_id, handle_capture(method, params, binary_payload))
        except CaptureError as exc:
            return error_response(request_id, exc.code, exc.params)
        except Exception as exc:
            return error_response(request_id, "capture.writer_failed", {"operation": method, "reason": str(exc)})
    if method in ("analysis_source_open", "analysis_source_read", "analysis_source_close",
                  "analysis_bundle_write", "analysis_bundle_publish", "analysis_bundle_discard"):
        try:
            return success_response(request_id, handle_analysis(method, params, binary_payload))
        except AnalysisError as exc:
            return error_response(request_id, exc.code, exc.params)
        except Exception as exc:
            return error_response(request_id, "analysis.export_failed" if method.startswith("analysis_bundle")
                                  else "analysis.source_invalid", {"path": str(params.get("path", "")), "reason": str(exc)})
    handlers = {
        "open": _handle_open,
        "get_slice": _handle_get_slice,
        "close": _handle_close,
        "export_markers_csv": _handle_export_markers_csv,
        "record_open": _handle_record_open,
        "record_event": _handle_record_event,
        "record_close": _handle_record_close,
        "export_segment": _handle_export_segment,
    }
    if method == "record_write":
        return _handle_record_write(request_id, params, binary_payload)
    if method == "export_samples":
        return _handle_export_samples(request_id, params, binary_payload)
    handler = handlers.get(method)
    if handler is None:
        return error_response(request_id, "protocol.unknown_method", {"method": str(method)})
    return handler(request_id, params)
