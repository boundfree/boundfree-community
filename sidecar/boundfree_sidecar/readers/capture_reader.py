"""Read-only capture.v1 playback, preserving calibration and sparse sample time.

The standard file/slice IPC is a display projection. Original integer/float64
samples and capture provenance remain in the recording; no converted file is
created. Each independently clocked stream is opened separately.
"""

import hashlib
import json
import math
from pathlib import Path

from .base import FileMetadata, Reader, SliceData
from .errors import CaptureReaderError, OptionalDependencyMissing
from ..capture_io.model import CaptureError, DTYPES, descriptor, u64

MAX_MANIFEST_BYTES = 1024 * 1024
MAX_DISPLAY_BYTES = 8 * 1024 * 1024


class CaptureReader(Reader):
    format_name = "BoundFree Capture"

    def __init__(self, path: Path, stream_key: str = "") -> None:
        super().__init__(path)
        self.directory = path if path.is_dir() else path.parent
        self.signals = path if path.suffix.lower() in (".h5", ".hdf5") else self.directory / "signals.h5"
        self.requested_stream = stream_key
        self._metadata = None

    def _fail(self, code, **params):
        raise CaptureReaderError(code, {"path": str(self.path), **params})

    @staticmethod
    def _h5py():
        try:
            import h5py
        except ImportError as exc:
            raise OptionalDependencyMissing("h5py", str(exc)) from exc
        return h5py

    def _group(self, file):
        h5py = self._h5py()
        group = file
        for part in self.group_path.split("/"):
            if not isinstance(group.get(part, getlink=True), h5py.HardLink):
                self._fail("reader.capture_invalid", reason="external or soft stream links are not supported")
            group = group[part]
        for name in ("samples", "blocks", "gaps"):
            if not isinstance(group.get(name, getlink=True), h5py.HardLink):
                self._fail("reader.capture_invalid", reason="external or soft dataset links are not supported")
            dataset = group[name]
            if not isinstance(dataset, h5py.Dataset) or dataset.is_virtual or dataset.external:
                self._fail("reader.capture_invalid", reason="external or virtual sample storage is not supported")
        return group

    def metadata(self) -> FileMetadata:
        if self._metadata is not None:
            return self._metadata
        manifest_path = self.directory / "manifest.json"
        if not manifest_path.is_file():
            self._fail("reader.capture_missing_manifest")
        try:
            with manifest_path.open("rb") as file:
                raw = file.read(MAX_MANIFEST_BYTES + 1)
            if len(raw) > MAX_MANIFEST_BYTES:
                raise ValueError("manifest exceeds 1 MiB")
            manifest = json.loads(raw.decode("utf-8"))
            if not isinstance(manifest, dict) or manifest.get("schema") != "boundfree.capture.v1":
                raise ValueError("unsupported capture schema")
            if not isinstance(manifest.get("finalized"), bool):
                raise ValueError("invalid finalized state")
            if not manifest["finalized"]:
                self._fail("reader.capture_incomplete")
            streams = manifest.get("streams")
            if not isinstance(streams, list) or not 1 <= len(streams) <= 64:
                raise ValueError("invalid stream list")
            choices = []
            identities = set()
            for entry in streams:
                d = descriptor(entry["descriptor"])
                key = f"{d['session_id']}/{d['stream_id']}"
                if key in identities:
                    raise ValueError("duplicate stream identity")
                identities.add(key)
                summary = entry.get("summary", {})
                if not isinstance(summary, dict):
                    raise ValueError("invalid stream summary")
                u64(summary.get("written_samples"), "summary.written_samples")
                choices.append({"key": key, "name": d.get("name") or str(d["stream_id"]),
                                "fs": d["rate_num"] / d["rate_den"], "channels": len(d["channels"]),
                                "samples": summary.get("written_samples", "0")})
            selected = self.requested_stream or next((s["key"] for s in choices if s["samples"] != "0"), choices[0]["key"])
            if selected not in identities:
                self._fail("reader.capture_invalid", reason="selected stream does not exist")
            entry = streams[next(i for i, s in enumerate(choices) if s["key"] == selected)]
            self.descriptor = d = entry["descriptor"]
            self.group_path = f"streams/{selected}"
            self.origin = u64(d.get("record_start_sample_index", "0"), "record_start_sample_index")
            self.fs = d["rate_num"] / d["rate_den"]
            if not math.isfinite(self.fs) or self.fs <= 0:
                raise ValueError("invalid stream rate")
            self.units = [c.get("unit") or "unknown" for c in d["channels"]]
            self.unit = self.units[0] if len(set(self.units)) == 1 else "mixed"
            with self._h5py().File(self.signals, "r") as file:
                g = self._group(file)
                data, blocks = g["samples"], g["blocks"]
                if data.ndim != 2 or data.shape[1] != len(self.units) or data.dtype.str != DTYPES[d["dtype"]][1]:
                    raise ValueError("invalid sample layout or dtype")
                required = {"sample_index", "row_offset", "sample_count", "descriptor_revision", "clock_id", "sha256"}
                if blocks.ndim != 1 or not required.issubset(set(blocks.dtype.names or ())):
                    raise ValueError("invalid block index")
                self.rows = int(data.shape[0])
                self.block_count = len(blocks)
                if not self.rows or not self.block_count:
                    self._fail("reader.capture_empty")
                # Validate the complete index in bounded chunks, not the whole
                # signal. Slice reads verify checksums of the blocks they use.
                previous_end, row_end = self.origin, 0
                for pos in range(0, len(blocks), 4096):
                    for block in blocks[pos:pos + 4096]:
                        first, row, count = (int(block[k]) for k in ("sample_index", "row_offset", "sample_count"))
                        if (first < previous_end or row != row_end or count <= 0 or
                            count > d["max_samples_per_block"] or count * len(self.units) * data.dtype.itemsize > 2 * 1024 * 1024 or
                            row + count > self.rows or first + count > (1 << 64) - 1 or
                            int(block["descriptor_revision"]) != d["descriptor_revision"] or
                            int(block["clock_id"]) != d["clock"]["clock_id"]):
                            raise ValueError("invalid block interval or identity")
                        previous_end, row_end = first + count, row + count
                if row_end != self.rows:
                    raise ValueError("unindexed sample rows")
                summary = entry.get("summary", {})
                if summary.get("written_samples") != str(self.rows) or summary.get("last_accepted_sample_exclusive") != str(previous_end):
                    raise ValueError("capture summary disagrees with stored blocks")
                gap_records = len(g["gaps"])
            self.duration = (previous_end - self.origin) / self.fs
            if not math.isfinite(self.duration) or self.duration <= 0:
                raise ValueError("invalid capture duration")
            info = {"schema": "boundfree.capture.v1", "status": manifest.get("status", "unknown"),
                    "stream_key": selected, "streams": choices, "channel_ids": [c["channel_id"] for c in d["channels"]], "original_dtype": d["dtype"],
                    "record_start_sample_index": str(self.origin), "gap_records": str(gap_records),
                    "unknown_gap": bool(summary.get("unknown_gap")), "clock_source": d["clock"]["source"],
                    "time_basis": "recording_relative_sample_index_at_declared_rate",
                    "simulated": bool(d["clock"].get("simulated", False)),
                    "sample_index_source": d.get("sample_index_source", "device"), "read_only": True}
            self._metadata = FileMetadata(self.fs, len(self.units), self.rows, self.duration,
                                          [c["name"] for c in d["channels"]], self.unit, self.format_name, [], self.units, info)
            return self._metadata
        except (CaptureReaderError, OptionalDependencyMissing):
            raise
        except (OSError, ValueError, TypeError, KeyError, CaptureError) as exc:
            self._fail("reader.capture_invalid", reason=str(exc))

    def get_slice(self, t0: float, t1: float) -> SliceData:
        import numpy as np
        meta = self.metadata()
        first = self.origin + max(0, int(math.ceil(t0 * self.fs - 1e-7)))
        stop = self.origin + max(0, int(math.ceil(t1 * self.fs - 1e-7)))
        pieces, times = [], []
        points = 0
        try:
            with self._h5py().File(self.signals, "r") as file:
                g = self._group(file); blocks, data = g["blocks"], g["samples"]
                if data.shape != (self.rows, len(self.units)) or len(blocks) != self.block_count:
                    self._fail("reader.capture_invalid", reason="recording changed after opening")
                # Find the first block whose exclusive end crosses the window.
                low, high = 0, self.block_count
                while low < high:
                    middle = (low + high) // 2; b = blocks[middle]
                    if int(b["sample_index"]) + int(b["sample_count"]) <= first: low = middle + 1
                    else: high = middle
                previous_end = None
                for index in range(low, self.block_count):
                    b = blocks[index]; start, row, count = (int(b[k]) for k in ("sample_index", "row_offset", "sample_count"))
                    if start >= stop: break
                    if (count <= 0 or count > self.descriptor["max_samples_per_block"] or row + count > self.rows or
                        count * len(self.units) * data.dtype.itemsize > 2 * 1024 * 1024):
                        self._fail("reader.capture_invalid", reason="invalid block bounds")
                    raw = data[row:row + count, :]
                    if hashlib.sha256(raw.tobytes(order="C")).digest() != bytes(b["sha256"]):
                        self._fail("reader.capture_corrupt_block", sample_index=str(start))
                    begin, end = max(start, first), min(start + count, stop)
                    if end <= begin: continue
                    extra = end - begin + (1 if previous_end is not None and begin != previous_end else 0)
                    points += extra
                    if points * (len(self.units) * 4 + 8) > MAX_DISPLAY_BYTES:
                        self._fail("reader.capture_slice_too_large")
                    if previous_end is not None and begin != previous_end:
                        pieces.append(np.full((1, len(self.units)), np.nan))
                        times.append(np.array([(begin - self.origin) / self.fs]))
                    values = raw[begin - start:end - start, :].astype(np.float64)
                    for ch, calibration in enumerate(self.descriptor["channels"]):
                        values[:, ch] = values[:, ch] * calibration["scale"] + calibration["offset"]
                    if not np.isfinite(values).all() or np.any(np.abs(values) > np.finfo(np.float32).max):
                        self._fail("reader.capture_invalid", reason="sample outside finite display range")
                    pieces.append(values)
                    times.append(((begin - self.origin) + np.arange(end - begin, dtype=np.float64)) / self.fs)
                    previous_end = end
            if pieces:
                values, coordinates = np.concatenate(pieces, axis=0).T, np.concatenate(times)
            else:
                values, coordinates = np.full((len(self.units), 2), np.nan), np.array([t0, t1])
            return SliceData(t0, t1, meta.fs, meta.unit, values.tolist(), self.units, coordinates.tolist())
        except (CaptureReaderError, OptionalDependencyMissing):
            raise
        except (OSError, ValueError, TypeError, KeyError) as exc:
            self._fail("reader.capture_invalid", reason=str(exc))
