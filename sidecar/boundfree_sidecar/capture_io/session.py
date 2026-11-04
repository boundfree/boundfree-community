"""Synchronous raw writes, bounded background flush, and capture finalization."""

import errno
import hashlib
import json
import os
import threading
import time
from pathlib import Path
from uuid import uuid4

from .model import (CaptureError, DTYPES, block_params, decode, i64,
                    identity, invalid, json_value, obj, open_params, string, u32, u64, uuid)

FLUSH_INTERVAL_SECONDS = 1.0
FLUSH_BYTES = 4 * 1024 * 1024


def atomic_manifest(path, manifest):
    temporary = path.with_name(f".{path.name}.{uuid4().hex}.tmp")
    try:
        with temporary.open("x", encoding="utf-8") as file:
            json.dump(manifest, file, ensure_ascii=False, allow_nan=False, indent=2)
            file.write("\n")
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except OSError:
            pass


def storage_error(exc, operation):
    current = exc
    disk_full = False
    while current is not None:
        if isinstance(current, OSError) and (current.errno in (errno.ENOSPC, getattr(errno, "EDQUOT", -1)) or
                "no space left on device" in str(current).lower() or "disk quota exceeded" in str(current).lower()):
            disk_full = True
            break
        current = current.__cause__ or current.__context__
    return CaptureError("capture.disk_full" if disk_full else "capture.writer_failed",
                        {"operation": operation, "reason": str(exc)})


def line(file, record):
    file.write(json_value(record, "record").decode("utf-8") + "\n")


def index_dtype():
    import numpy as np
    return np.dtype([
        ("sample_index", "<u8"), ("row_offset", "<u8"), ("sample_count", "<u4"),
        ("first_timestamp_ticks", "<u8"), ("host_received_monotonic_ns", "<u8"),
        ("clock_id", "<u4"), ("time_flags", "<u2"), ("quality_flags", "<u4"),
        ("descriptor_revision", "<u4"), ("sha256", "u1", (32,)),
    ])


def gap_dtype():
    import numpy as np
    return np.dtype([("start", "<u8"), ("count", "<u8"), ("start_known", "u1"),
                     ("count_known", "u1"), ("source", "u1")])


class CaptureSession:
    def __init__(self, path, handle, host, descriptors, metadata):
        self.path, self.handle = path, handle
        self.lock = threading.RLock()
        self.stop_timer = threading.Event()
        self.closed = False
        self.failure = None
        self.dirty = 0
        self.streams = {}
        self.h5 = self.events = self.mappings = None
        self.thread = None
        self.manifest = {
            "schema": "boundfree.capture.v1", "capture_file_id": path.name[len("capture-"):],
            "finalized": False, "status": "recording", "host_clock": host,
            "streams": [{"descriptor": d, "storage_dtype": DTYPES[d["dtype"]][1],
                         "original_dtype": d["dtype"], "valid_bits": 24 if d["dtype"] == "int24" else None}
                        for d in descriptors],
            "storage": {"flush_interval_seconds": 1, "flush_bytes": FLUSH_BYTES,
                        "durable_samples": None, "fsync_verified": False},
        }
        if metadata is not None:
            self.manifest["acquisition_metadata"] = metadata
        atomic_manifest(path / "manifest.json", self.manifest)
        try:
            import h5py
            self.h5 = h5py.File(path / "signals.h5", "x")
            self.events = (path / "events.jsonl").open("x", encoding="utf-8")
            self.mappings = (path / "clock-mappings.jsonl").open("x", encoding="utf-8")
            for descriptor in descriptors:
                key = (descriptor["session_id"], descriptor["stream_id"])
                group = self.h5.require_group(f"streams/{key[0]}/{key[1]}")
                channels = len(descriptor["channels"])
                row_bytes = channels * int(DTYPES[descriptor["dtype"]][1][-1])
                chunk_rows = max(1, min(1024, 256 * 1024 // row_bytes))
                data = group.create_dataset("samples", shape=(0, channels), maxshape=(None, channels),
                                            chunks=(chunk_rows, channels), dtype=DTYPES[descriptor["dtype"]][1])
                index = group.create_dataset("blocks", shape=(0,), maxshape=(None,), chunks=(256,), dtype=index_dtype())
                gaps = group.create_dataset("gaps", shape=(0,), maxshape=(None,), chunks=(256,), dtype=gap_dtype())
                self.streams[key] = {"descriptor": descriptor, "data": data, "index": index, "gaps": gaps,
                                     "next": int(descriptor["record_start_sample_index"]),
                                     "accepted": 0, "written": 0, "flushed": 0,
                                     "gap_records": 0, "duplicates": 0, "partial": False}
            self.h5.flush()
            self.events.flush()
            self.mappings.flush()
        except Exception as exc:
            self._fail(exc, "open")
            self._close_files()
            raise self.failure from exc
        self.thread = threading.Thread(target=self._flush_loop, name=f"capture-flush-{handle}", daemon=True)
        self.thread.start()

    def _fail(self, exc, operation):
        if self.failure is None:
            self.failure = storage_error(exc, operation)
            self.manifest["status"] = "failed"
            self.manifest["failure"] = {"code": self.failure.code, "params": self.failure.params}
            self.manifest["finalized"] = False
            try:
                atomic_manifest(self.path / "manifest.json", self.manifest)
            except Exception:
                pass  # The caller retains the failure when the disk is not writable.
        self.stop_timer.set()

    def _check(self):
        if self.failure is not None:
            raise self.failure
        if self.closed:
            raise CaptureError("capture.session_not_found", {"handle": self.handle})

    def _state(self, raw_identity):
        ident = identity(raw_identity)
        state = self.streams.get((ident["session_id"], ident["stream_id"]))
        if state is None or ident != identity(state["descriptor"], "descriptor"):
            invalid("stream", "identity or revision does not match")
        return state

    def _counters(self, state):
        return {"accepted_samples": str(state["accepted"]), "written_samples": str(state["written"]),
                "flushed_samples": str(state["flushed"]), "durable_samples": None,
                "last_accepted_sample_exclusive": str(state["next"]),
                "gap_records": str(state["gap_records"]), "duplicate_blocks": str(state["duplicates"])}

    def _record_gap(self, state, start, count, source, reason, detection):
        gaps = state["gaps"]
        row = len(gaps)
        gaps.resize((row + 1,))
        gaps[row] = (start or 0, count or 0, int(start is not None), int(count is not None), source)
        record = {"event_id": str(uuid4()), "kind": "data.gap", "stream": identity(state["descriptor"]),
                  "sample_index": str(start) if start is not None else None,
                  "count": str(count) if count is not None else None,
                  "params": {"reason": reason, "detection": detection}}
        line(self.events, record)
        state["gap_records"] += 1
        state["partial"] = True
        self.dirty += len(json_value(record, "gap"))

    def _find_duplicate(self, state, first, count, digest, timestamp, time_flags, quality_flags):
        blocks = state["index"]
        lo, hi = 0, len(blocks)
        while lo < hi:
            mid = (lo + hi) // 2
            if int(blocks[mid]["sample_index"]) < first:
                lo = mid + 1
            else:
                hi = mid
        if lo == len(blocks):
            return False
        block = blocks[lo]
        return (int(block["sample_index"]) == first and int(block["sample_count"]) == count and
                bytes(block["sha256"]) == digest and int(block["time_flags"]) == time_flags and
                int(block["quality_flags"]) == quality_flags and
                int(block["first_timestamp_ticks"]) == (timestamp or 0))

    def append(self, params, payload):
        import numpy as np
        with self.lock:
            self._check()
            state = self._state(params.get("stream"))
            descriptor = state["descriptor"]
            channels, count, first, timestamp, host, time_flags, quality_flags = block_params(params, descriptor, payload)
            try:
                samples = decode(payload, descriptor["dtype"], channels, count)
            except CaptureError:
                # A validly identified, non-finite block is a known data loss;
                # rejecting it must not permit a later false "complete" result.
                if first >= state["next"]:
                    try:
                        self._record_gap(state, first, count, 5, "non_finite_samples", "payload_validation")
                    except Exception as exc:
                        self._fail(exc, "append")
                        raise self.failure from exc
                raise
            digest = hashlib.sha256(samples.tobytes(order="C")).digest()
            if first < state["next"]:
                try:
                    duplicate = self._find_duplicate(state, first, count, digest, timestamp, time_flags, quality_flags)
                except Exception as exc:
                    self._fail(exc, "append")
                    raise self.failure from exc
                if duplicate:
                    state["duplicates"] += 1
                    return {"accepted": False, "duplicate": True, **self._counters(state)}
                invalid("first_sample_index", "overlapping or out-of-order block")
            try:
                if first > state["next"]:
                    self._record_gap(state, state["next"], first - state["next"], 1, "missing_samples", "sample_index")
                if quality_flags & 4:
                    self._record_gap(state, None, None, 2, "unknown_length", "quality_flag")
                data, blocks = state["data"], state["index"]
                offset = data.shape[0]
                data.resize((offset + count, channels))
                data[offset:offset + count, :] = samples
                i = len(blocks)
                blocks.resize((i + 1,))
                blocks[i] = (first, offset, count, timestamp or 0, host, descriptor["clock"]["clock_id"],
                             time_flags, quality_flags, descriptor["descriptor_revision"],
                             np.frombuffer(digest, dtype="u1"))
                state["next"] = first + count
                state["accepted"] += count
                state["written"] += count
                self.dirty += len(payload)
                if self.dirty >= FLUSH_BYTES:
                    self._flush()
            except Exception as exc:
                self._fail(exc, "append")
                raise self.failure from exc
            return {"accepted": True, "duplicate": False, **self._counters(state)}

    def event(self, params):
        with self.lock:
            self._check()
            record_type = params.get("record_type")
            if record_type not in ("event", "clock_mapping"):
                invalid("record_type", "required event or clock_mapping")
            state = None if params.get("stream") is None else self._state(params["stream"])
            record = obj(params.get("record"), "record").copy()
            if "stream" in record:
                invalid("record.stream", "use top-level stream identity")
            if state is not None:
                record["stream"] = identity(state["descriptor"])
            if record_type == "event":
                uuid(record.get("event_id"), "record.event_id")
                kind = string(record.get("kind"), "record.kind")
                obj(record.get("params"), "record.params")
                if "clock_id" in record:
                    clock_id = u32(record["clock_id"], "record.clock_id")
                    if state is None or clock_id != state["descriptor"]["clock"]["clock_id"]:
                        invalid("record.clock_id", "does not match stream")
                start = u64(record.get("sample_index"), "record.sample_index", True)
                count = u64(record.get("count"), "record.count", True)
                if count == 0 or (start is not None and count is not None and start + count > (1 << 64) - 1):
                    invalid("record.count", "invalid gap interval")
                if kind == "data.gap":
                    if state is None:
                        invalid("stream", "gap requires stream identity")
                    string(record["params"].get("reason"), "record.params.reason")
                    string(record["params"].get("detection"), "record.params.detection")
            else:
                if state is None:
                    invalid("stream", "mapping requires stream identity")
                clock_id = u32(record.get("clock_id"), "record.clock_id")
                if clock_id != state["descriptor"]["clock"]["clock_id"]:
                    invalid("record.clock_id", "does not match stream")
                u32(record.get("mapping_revision"), "record.mapping_revision", 1)
            for field in ("ticks", "valid_from_ticks", "valid_to_ticks", "host_monotonic_ns", "uncertainty_ns",
                          "client_send_ns", "client_receive_ns", "device_receive_ticks", "device_send_ticks"):
                if field in record:
                    u64(record[field], f"record.{field}", True)
            if "offset_ns" in record:
                i64(record["offset_ns"], "record.offset_ns")
            json_value(record, "record")
            try:
                if record_type == "event" and record["kind"] == "data.gap":
                    gaps = state["gaps"]
                    i = len(gaps)
                    gaps.resize((i + 1,))
                    gaps[i] = (start or 0, count or 0, int(start is not None), int(count is not None), 3)
                    state["gap_records"] += 1
                    state["partial"] = True
                line(self.events if record_type == "event" else self.mappings, record)
                self.dirty += len(json_value(record, "record"))
                if self.dirty >= FLUSH_BYTES:
                    self._flush()
            except Exception as exc:
                self._fail(exc, "event")
                raise self.failure from exc
            return {"accepted": True}

    def _flush(self):
        self.h5.flush()
        self.events.flush()
        self.mappings.flush()
        for state in self.streams.values():
            state["flushed"] = state["written"]
        self.dirty = 0

    def _flush_loop(self):
        while not self.stop_timer.wait(FLUSH_INTERVAL_SECONDS):
            with self.lock:
                if self.dirty and not self.closed and self.failure is None:
                    try:
                        self._flush()
                    except Exception as exc:
                        self._fail(exc, "flush")

    def _summaries(self, params):
        summaries = params.get("streams")
        if not isinstance(summaries, list) or len(summaries) != len(self.streams):
            invalid("streams", "one summary per stream required")
        result, keys = [], set()
        for summary in summaries:
            obj(summary, "summary")
            state = self._state(summary.get("stream"))
            key = (state["descriptor"]["session_id"], state["descriptor"]["stream_id"])
            if key in keys:
                invalid("streams", "duplicate stream summary")
            keys.add(key)
            last = u64(summary.get("last_accepted_sample_exclusive"), "last_accepted_sample_exclusive")
            if last != state["next"]:
                invalid("last_accepted_sample_exclusive", "does not match accepted boundary")
            terminal = u64(summary.get("terminal_sample_exclusive"), "terminal_sample_exclusive", True)
            if terminal is not None and terminal < last:
                invalid("terminal_sample_exclusive", "before accepted boundary")
            if not isinstance(summary.get("complete"), bool):
                invalid("complete", "required boolean")
            dropped = u64(summary.get("dropped_samples", "0"), "dropped_samples", True)
            unknown = summary.get("unknown_gap", False)
            if not isinstance(unknown, bool):
                invalid("unknown_gap", "required boolean")
            result.append((state, summary, terminal, dropped, unknown))
        return result

    def finalize(self, params):
        with self.lock:
            self._check()
            summaries = self._summaries(params)
        self.stop_timer.set()
        if self.thread is not None:
            self.thread.join()
        with self.lock:
            self._check()
            try:
                for state, summary, terminal, dropped, unknown in summaries:
                    if terminal is None or unknown:
                        self._record_gap(state, None, None, 4, "unknown_end_or_gap", "final_summary")
                    if terminal is not None and terminal > state["next"]:
                        self._record_gap(state, state["next"], terminal - state["next"], 4, "missing_tail", "final_summary")
                    if not summary["complete"] or dropped is None or dropped > 0:
                        state["partial"] = True
                self._flush()
                self._close_files(raise_errors=True)
                self.closed = True
                partial = any(state["partial"] for state in self.streams.values())
                for manifest_stream in self.manifest["streams"]:
                    d = manifest_stream["descriptor"]
                    state = self.streams[(d["session_id"], d["stream_id"])]
                    summary = next(s for st, s, _, _, _ in summaries if st is state)
                    manifest_stream["summary"] = {**self._counters(state),
                        "terminal_sample_exclusive": summary.get("terminal_sample_exclusive"),
                        "complete": summary["complete"], "dropped_samples": summary.get("dropped_samples", "0"),
                        "unknown_gap": summary.get("unknown_gap", False)}
                self.manifest["status"] = "partial" if partial else "complete"
                self.manifest["finalized"] = True
                atomic_manifest(self.path / "manifest.json", self.manifest)
            except Exception as exc:
                self._fail(exc, "finalize")
                self._close_files()
                self.closed = True
                raise self.failure from exc
            return {"path": str(self.path), "finalized": True, "status": self.manifest["status"],
                    "streams": [{"stream": identity(x["descriptor"]), **x["summary"]} for x in self.manifest["streams"]]}

    def _close_files(self, raise_errors=False):
        failure = None
        for name in ("h5", "events", "mappings"):
            file = getattr(self, name)
            setattr(self, name, None)
            if file is not None:
                try:
                    file.close()
                except Exception as exc:
                    if failure is None:
                        failure = exc
        if failure is not None and raise_errors:
            raise failure

    def abort(self):
        self.stop_timer.set()
        if self.thread is not None and self.thread is not threading.current_thread():
            self.thread.join()
        with self.lock:
            if self.closed:
                return
            if self.failure is None:
                try:
                    self._flush()
                    self._close_files(raise_errors=True)
                    self.manifest["status"] = "interrupted"
                    atomic_manifest(self.path / "manifest.json", self.manifest)
                except Exception as exc:
                    self._fail(exc, "cleanup")
            self._close_files()
            self.closed = True


class CaptureRegistry:
    def __init__(self):
        self.sessions = {}

    def open(self, params):
        directory, host, descriptors, metadata = open_params(params)
        handle = str(uuid4())
        path = Path(directory) / f"capture-{handle}"
        try:
            path.mkdir(exist_ok=False)
            session = CaptureSession(path, handle, host, descriptors, metadata)
        except CaptureError:
            raise
        except Exception as exc:
            raise storage_error(exc, "open") from exc
        self.sessions[handle] = session
        return {"handle": handle, "path": str(path), "streams": [identity(d) for d in descriptors]}

    def _get(self, params):
        handle = string(params.get("handle"), "handle")
        if handle not in self.sessions:
            raise CaptureError("capture.session_not_found", {"handle": handle})
        return self.sessions[handle]

    def append(self, params, payload):
        return self._get(params).append(params, payload)

    def event(self, params):
        return self._get(params).event(params)

    def finalize(self, params):
        session = self._get(params)
        try:
            return session.finalize(params)
        except CaptureError:
            if session.failure is not None:
                session.abort()
            raise
        finally:
            if session.closed:
                self.sessions.pop(session.handle, None)

    def cleanup(self):
        for session in list(self.sessions.values()):
            session.abort()
        self.sessions.clear()
