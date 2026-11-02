"""Read-only, bounded inspection of a raw capture directory."""

import hashlib
import json
from pathlib import Path

from .model import CaptureError, DTYPES, MAX_JSON_BYTES, descriptor, identity, string, validate_acquisition_metadata

MAX_MANIFEST_BYTES = 1024 * 1024
MAX_READ_BYTES = 1024 * 1024
MAX_ISSUES = 100


def inspect(params):
    path = Path(string(params.get("path"), "path"))
    issues = []

    def issue(code, **details):
        if len(issues) < MAX_ISSUES:
            issues.append({"code": code, **details})

    result = {"path": str(path), "finalized": False, "status": "unknown", "streams": [],
              "issues": issues, "recovery_advice": []}
    try:
        with (path / "manifest.json").open("rb") as file:
            raw = file.read(MAX_MANIFEST_BYTES + 1)
        if len(raw) > MAX_MANIFEST_BYTES:
            raise ValueError("manifest exceeds limit")
        manifest = json.loads(raw.decode("utf-8"))
        if not isinstance(manifest, dict) or manifest.get("schema") != "boundfree.capture.v1":
            raise ValueError("unsupported manifest")
        streams = manifest.get("streams")
        if not isinstance(streams, list) or not 1 <= len(streams) <= 64:
            raise ValueError("invalid stream list")
        if not isinstance(manifest.get("finalized"), bool):
            raise ValueError("invalid finalized state")
        result["finalized"] = manifest["finalized"]
        result["status"] = manifest.get("status", "unknown")
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        issue("manifest_unreadable", reason=str(exc))
        result["recovery_advice"] = ["preserve_original", "copy_before_recovery"]
        result["verification"] = "issues"
        return result

    if "acquisition_metadata" in manifest:
        try:
            result["acquisition_metadata"] = validate_acquisition_metadata(manifest["acquisition_metadata"])
        except CaptureError as exc:
            issue("acquisition_metadata_invalid", **exc.params)

    for filename in ("events.jsonl", "clock-mappings.jsonl"):
        count = 0
        try:
            with (path / filename).open("rb") as file:
                while True:
                    raw = file.readline(MAX_JSON_BYTES + 2)
                    if not raw:
                        break
                    count += 1
                    if len(raw) > MAX_JSON_BYTES + 1 or not raw.endswith(b"\n"):
                        issue("log_invalid", file=filename, line=str(count))
                        break
                    try:
                        value = json.loads(raw.decode("utf-8"))
                        if not isinstance(value, dict):
                            raise ValueError("record is not an object")
                    except (ValueError, UnicodeError):
                        issue("log_invalid", file=filename, line=str(count))
                        break
        except OSError as exc:
            issue("log_unreadable", file=filename, reason=str(exc))
        result[filename.replace(".jsonl", "_count").replace("-", "_")] = str(count)

    try:
        import h5py
        with h5py.File(path / "signals.h5", "r") as h5:
            seen = set()
            for manifest_stream in streams:
                try:
                    d = descriptor(manifest_stream["descriptor"])
                    ident = identity(d, "descriptor")
                    key = (d["session_id"], d["stream_id"])
                    if key in seen:
                        raise ValueError("duplicate stream")
                    seen.add(key)
                    group = h5[f"streams/{key[0]}/{key[1]}"]
                    data, blocks, gaps = group["samples"], group["blocks"], group["gaps"]
                    if data.ndim != 2 or data.shape[1] != len(d["channels"]) or data.dtype.str != DTYPES[d["dtype"]][1]:
                        raise ValueError("sample shape or dtype mismatch")
                    if blocks.ndim != 1 or gaps.ndim != 1:
                        raise ValueError("invalid index shape")
                    required = {"sample_index", "row_offset", "sample_count", "first_timestamp_ticks",
                                "host_received_monotonic_ns", "clock_id", "time_flags", "quality_flags",
                                "descriptor_revision", "sha256"}
                    if not required.issubset(set(blocks.dtype.names or ())):
                        raise ValueError("invalid block index")
                    row_end = checked_samples = verified_samples = 0
                    sample_end = int(d.get("record_start_sample_index", "0"))
                    checked_blocks = verified_blocks = 0
                    rows_per_read = max(1, MAX_READ_BYTES // (data.shape[1] * data.dtype.itemsize))
                    for offset in range(0, len(blocks), 256):
                        for block in blocks[offset:offset + 256]:
                            first, row, count = (int(block["sample_index"]), int(block["row_offset"]), int(block["sample_count"]))
                            if (not count or first < sample_end or row != row_end or row + count > data.shape[0]
                                    or first + count > (1 << 64) - 1
                                    or int(block["descriptor_revision"]) != d["descriptor_revision"]
                                    or int(block["clock_id"]) != d["clock"]["clock_id"]):
                                issue("block_index_invalid", stream=ident, block=str(checked_blocks))
                                raise ValueError("invalid block index or sample interval")
                            digest = hashlib.sha256()
                            for pos in range(row, row + count, rows_per_read):
                                digest.update(data[pos:min(row + count, pos + rows_per_read), :].tobytes(order="C"))
                            if digest.digest() != bytes(block["sha256"]):
                                issue("block_checksum_mismatch", stream=ident, sample_index=str(first))
                            else:
                                verified_samples += count
                                verified_blocks += 1
                            sample_end, row_end = first + count, row + count
                            checked_samples += count
                            checked_blocks += 1
                    if row_end != data.shape[0]:
                        issue("unindexed_sample_rows", stream=ident, count=str(data.shape[0] - row_end))
                    summary = manifest_stream.get("summary")
                    if result["finalized"] and (not isinstance(summary, dict) or
                            summary.get("written_samples") != str(checked_samples) or
                            summary.get("last_accepted_sample_exclusive") != str(sample_end)):
                        issue("summary_mismatch", stream=ident)
                    result["streams"].append({"stream": ident, "verified_blocks": str(verified_blocks),
                        "verified_samples": str(verified_samples), "last_sample_exclusive": str(sample_end),
                        "gap_records": str(len(gaps))})
                except (CaptureError, KeyError, TypeError, ValueError, OSError) as exc:
                    issue("stream_unreadable", reason=str(exc))
    except (OSError, ValueError, ImportError) as exc:
        issue("signals_unreadable", reason=str(exc))

    if not result["finalized"] or result["status"] != "complete" or issues:
        result["recovery_advice"] = ["preserve_original", "copy_before_recovery"]
    result["verification"] = "issues" if issues else "ok"
    return result
