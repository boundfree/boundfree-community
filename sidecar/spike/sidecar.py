import json
import os
import re
import struct
import sys

import numpy as np


REF = os.path.join(
    os.path.dirname(__file__),
    "..",
    "..",
    "..",
    "boundfree-eeg-upper-computer",
    "boundfree_eeg",
)
sys.path.insert(0, os.path.abspath(REF))

from eeg_io.readers import read_file_lazy  # noqa: E402


_reader = None


class SyntheticReader:
    def __init__(self, n_channels, fs, duration):
        self.fs = float(fs)
        self.n_channels = int(n_channels)
        self.duration = float(duration)
        self.ch_names = [f"Ch{i + 1}" for i in range(self.n_channels)]
        self.markers = []

    def get_slice(self, t_start, t_end):
        t_start = max(0.0, float(t_start))
        t_end = min(self.duration, float(t_end))
        if t_end <= t_start:
            return np.zeros((self.n_channels, 0), dtype=np.float32)
        n_samp = int(np.ceil((t_end - t_start) * self.fs))
        x = np.linspace(t_start, t_end, n_samp, endpoint=False, dtype=np.float32)
        ch = np.arange(self.n_channels, dtype=np.float32)[:, None]
        return 50.0 * np.sin(2.0 * np.pi * (8.0 + ch * 0.01) * x[None, :])


def _open_reader(path):
    match = re.fullmatch(r"synthetic://(\d+)x(\d+)x(\d+(?:\.\d+)?)", path)
    if match:
        n_channels, fs, duration = match.groups()
        return SyntheticReader(int(n_channels), float(fs), float(duration))
    return read_file_lazy(path)


def _read_frame():
    header = sys.stdin.buffer.read(4)
    if len(header) < 4:
        return None
    n_bytes = struct.unpack(">I", header)[0]
    payload = sys.stdin.buffer.read(n_bytes)
    if len(payload) < n_bytes:
        return None
    return json.loads(payload.decode("utf-8"))


def _write_json(obj):
    payload = json.dumps(obj, ensure_ascii=False).encode("utf-8")
    sys.stdout.buffer.write(struct.pack(">I", len(payload)))
    sys.stdout.buffer.write(payload)
    sys.stdout.buffer.flush()


def _write_array(arr):
    arr = np.ascontiguousarray(arr, dtype=np.float32)
    payload = struct.pack(">ii", arr.shape[0], arr.shape[1]) + arr.tobytes()
    sys.stdout.buffer.write(struct.pack(">I", len(payload)))
    sys.stdout.buffer.write(payload)
    sys.stdout.buffer.flush()


def _markers(reader):
    values = []
    for marker in getattr(reader, "markers", []) or []:
        values.append(
            {
                "time": float(getattr(marker, "time", 0.0)),
                "label": str(getattr(marker, "label", "")),
            }
        )
    return values


def _handle(req):
    global _reader

    method = req.get("method")
    if method == "open":
        _reader = _open_reader(req["path"])
        _write_json(
            {
                "fs": _reader.fs,
                "n_channels": _reader.n_channels,
                "duration": _reader.duration,
                "ch_names": list(_reader.ch_names),
                "markers": _markers(_reader),
            }
        )
    elif method == "get_slice":
        if _reader is None:
            _write_json({"error_code": "reader.not_open", "params": {}})
            return
        _write_array(_reader.get_slice(float(req["t0"]), float(req["t1"])))
    elif method == "close":
        _reader = None
        _write_json({"ok": True})
    else:
        _write_json({"error_code": "unknown_method", "params": {"method": method}})


def main():
    while True:
        req = _read_frame()
        if req is None:
            break
        try:
            _handle(req)
        except Exception as exc:  # noqa: BLE001 - spike returns structured errors.
            _write_json({"error_code": "sidecar.exception", "params": {"msg": str(exc)}})


if __name__ == "__main__":
    main()
