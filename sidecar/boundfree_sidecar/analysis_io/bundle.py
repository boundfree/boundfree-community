"""Verified float64 bundle staging and explicit publication."""
import atexit
import csv
import json
import math
import os
from pathlib import Path
import shutil
import tempfile
import uuid

import numpy as np

from .sources import MAX_BYTES, fail, fingerprint, positive_integer
from .errors import AnalysisError

GROUPS = ("original", "analysis", "eeg", "eog", "emg", "unresolved", "residual")
SCHEMA = "boundfree.community.offline-eeg.v1"


def _number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _error(data, mean):
    with np.errstate(over="ignore", invalid="ignore"):
        difference = data[1] - mean[:, None]
        for group in data[2:]:
            difference = difference - group
    if not np.isfinite(difference).all():
        fail("non_finite")
    return float(np.max(np.abs(difference)))


def _verify_source(source):
    if not isinstance(source, dict) or not isinstance(source.get("path"), str) or not isinstance(source.get("fingerprint"), dict):
        fail("invalid_request", field="manifest.source", reason="required path and fingerprint")
    path = Path(source["path"]).resolve()
    try:
        same = fingerprint(path) == source["fingerprint"]
    except OSError:
        same = False
    if not same:
        fail("source_changed", path=str(path))
    return path


class BundleRegistry:
    def __init__(self):
        self.stages = {}
        atexit.register(self.cleanup)

    def cleanup(self):
        for stage in list(self.stages.values()):
            shutil.rmtree(stage["temporary"], ignore_errors=True)
        self.stages.clear()

    def write(self, params, payload):
        cancel_path = params.get("cancel_path")
        if cancel_path is not None and (not isinstance(cancel_path, str) or not cancel_path):
            fail("invalid_request", field="cancel_path", reason="required nonempty control file path")
        _check_cancel(cancel_path)
        shape, names = params.get("shape"), params.get("ch_names")
        if (not isinstance(shape, list) or len(shape) != 3 or shape[0] != 7
                or not all(positive_integer(x) for x in shape) or not 2 <= shape[1] <= 32
                or shape[2] > 60000 or params.get("dtype") != "float64-le" or params.get("unit") not in ("uV","count","1","degC","A","mA","Pa","kPa")
                or not isinstance(names, list) or len(names) != shape[1]
                or not all(isinstance(x, str) and x for x in names)):
            fail("invalid_request", field="shape", reason="required [7,2..32,1..60000] float64-le uV and channel names")
        unit=params.get("unit")
        size = math.prod(shape) * 8
        if size > MAX_BYTES:
            fail("resource_limit", bytes=size, limit_bytes=MAX_BYTES)
        if payload is None or len(payload) != size:
            fail("invalid_request", field="payload", reason="byte length mismatch")
        fs, t0 = params.get("fs"), params.get("t0")
        if not _number(fs) or fs <= 0 or not _number(t0) or t0 < 0:
            fail("invalid_request", field="fs/t0", reason="required positive fs and nonnegative t0")
        manifest = params.get("manifest")
        if not isinstance(manifest, dict):
            fail("invalid_request", field="manifest", reason="required object")
        mean = manifest.get("mean")
        if not isinstance(mean, list) or len(mean) != shape[1] or not all(_number(x) for x in mean):
            fail("invalid_request", field="manifest.mean", reason="required finite channel mean")
        mean = np.asarray(mean, dtype="<f8")
        source_path = _verify_source(manifest.get("source"))
        if manifest["source"].get("unit","uV") != unit:
            fail("invalid_request",field="unit",reason="source and output units differ")
        value = params.get("path")
        if not isinstance(value, str) or not value:
            fail("invalid_request", field="path", reason="required new directory")
        destination = Path(value).absolute()
        if destination.exists() or destination.is_symlink():
            fail("destination_exists", path=str(destination))
        if not destination.parent.is_dir() or destination.resolve() == source_path:
            fail("export_failed", path=str(destination), reason="parent directory unavailable or source collision")
        data = np.frombuffer(payload, dtype="<f8").reshape(shape)
        if not np.isfinite(data).all():
            fail("invalid_request", field="payload", reason="non-finite samples")
        error = _error(data, mean)
        if error > 1e-6:
            fail("reconstruction_failed", error_uV=error)
        with np.errstate(over="ignore", invalid="ignore", divide="ignore"):
            expected_time = t0 + np.arange(shape[2], dtype=np.float64) / fs
        if not np.isfinite(expected_time).all():
            fail("invalid_request", field="fs/t0", reason="non-finite output time axis")
        token = uuid.uuid4().hex
        temporary = Path(tempfile.mkdtemp(prefix=".boundfree-analysis-", dir=destination.parent))
        stage = {"temporary": temporary, "destination": destination, "source": manifest["source"], "result": None, "cancel_path": cancel_path}
        self.stages[token] = stage
        try:
            outputs = {}
            readback = np.empty(shape, dtype="<f8")
            for index, group in enumerate(GROUPS):
                _check_cancel(cancel_path)
                path = temporary / (group + "_" + unit + ".csv")
                with path.open("w", newline="", encoding="utf-8") as output:
                    writer = csv.writer(output)
                    writer.writerow(["time_s", *names])
                    for sample in range(shape[2]):
                        if sample % 256 == 0:
                            _check_cancel(cancel_path)
                        writer.writerow([format(expected_time[sample], ".17g"),
                                         *(format(value, ".17g") for value in data[index, :, sample])])
                    _check_cancel(cancel_path)
                    output.flush()
                    os.fsync(output.fileno())
                with path.open(newline="", encoding="utf-8") as stream:
                    reader = csv.reader(stream)
                    if next(reader) != ["time_s", *names]:
                        fail("export_failed", path=str(path), reason="readback header mismatch")
                    for sample, row in enumerate(reader):
                        if sample % 256 == 0:
                            _check_cancel(cancel_path)
                        if len(row) != shape[1] + 1 or sample >= shape[2]:
                            fail("export_failed", path=str(path), reason="readback shape mismatch")
                        if float(row[0]) != expected_time[sample]:
                            fail("export_failed", path=str(path), reason="readback timestamp mismatch")
                        readback[index, :, sample] = [float(x) for x in row[1:]]
                    if sample + 1 != shape[2]:
                        fail("export_failed", path=str(path), reason="readback truncated")
                if not np.array_equal(readback[index], data[index]):
                    fail("export_failed", path=str(path), reason="float64 roundtrip mismatch")
                outputs[path.name] = {"sha256": fingerprint(path)["sha256"], "size": path.stat().st_size}
            roundtrip_error = _error(readback, mean)
            if roundtrip_error > 1e-6:
                fail("reconstruction_failed", error_uV=roundtrip_error)
            _check_cancel(cancel_path)
            _verify_source(manifest["source"])
            _check_cancel(cancel_path)
            summary = {**manifest, "schema": SCHEMA, "fs": fs, "t0": t0, "shape": shape,
                       "ch_names": names, "unit": unit, "dtype": "float64-le", "groups": list(GROUPS),
                       "group_identity": "analysis = mean + eeg + eog + emg + unresolved + residual",
                       "reconstruction_error_uV": error, "readback_error_uV": roundtrip_error, "files": outputs}
            if unit!="uV":
                summary["reconstruction_error"]=summary.pop("reconstruction_error_uV")
                summary["readback_error"]=summary.pop("readback_error_uV")
                summary["error_unit"]=unit
            path = temporary / "analysis.json"
            with path.open("w", encoding="utf-8") as stream:
                json.dump(summary, stream, ensure_ascii=False, indent=2, allow_nan=False)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            _check_cancel(cancel_path)
            # Parse the manifest again, not just the numeric CSVs.
            json.loads(path.read_text(encoding="utf-8"))
            outputs[path.name] = {"sha256": fingerprint(path)["sha256"], "size": path.stat().st_size}
            stage["result"] = {"path": str(destination), "files": outputs, "readback_error_uV" if unit=="uV" else "readback_error": roundtrip_error}
            if params.get("stage_only", False):
                return {**stage["result"], "token": token, "staged": True, "temporary_path": str(temporary)}
            return self.publish({"token": token})
        except BaseException:
            self.discard({"token": token})
            raise

    def _get(self, params):
        token = params.get("token")
        if not isinstance(token, str) or token not in self.stages:
            fail("handle_not_found", handle=str(token))
        return token, self.stages[token]

    def publish(self, params):
        token, stage = self._get(params)
        try:
            return self._publish(token, stage)
        except AnalysisError as exc:
            if exc.code == "analysis.cancelled":
                self.discard(params)
            raise

    def _publish(self, token, stage):
        destination = stage["destination"]
        _check_cancel(stage.get("cancel_path"))
        _verify_source(stage["source"])
        if destination.exists() or destination.is_symlink():
            fail("destination_exists", path=str(destination))
        # Reserve destination so even an empty existing directory is never replaced.
        # Publication uses a sibling rename. On POSIX renameat2 NOREPLACE is used.
        _check_cancel(stage.get("cancel_path"))
        _rename_no_replace(stage["temporary"], destination)
        del self.stages[token]
        return {**stage["result"], "staged": False, "published": True}

    def discard(self, params):
        token, stage = self._get(params)
        shutil.rmtree(stage["temporary"], ignore_errors=True)
        del self.stages[token]
        return {"discarded": True}


def _check_cancel(path):
    if path is not None and Path(path).exists():
        fail("cancelled")


def _rename_no_replace(source, destination):
    if os.name == "nt":
        os.rename(source, destination)  # Windows rename fails when destination exists.
        return
    import ctypes
    import errno
    libc = ctypes.CDLL(None, use_errno=True)
    renameat2 = getattr(libc, "renameat2", None)
    if renameat2 is None:
        fail("export_failed", path=str(destination), reason="atomic no-replace publication unavailable")
    result = renameat2(-100, os.fsencode(source), -100, os.fsencode(destination), 1)
    if result != 0:
        code = ctypes.get_errno()
        if code == errno.EEXIST:
            fail("destination_exists", path=str(destination))
        raise OSError(code, os.strerror(code), str(destination))
