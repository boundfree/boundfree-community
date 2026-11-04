"""Bounded, double precision source snapshots for offline analysis.

Intan decoding stays in Neo RawIO. The small subclass replaces Neo's whole-file
flatten/diff timestamp check with bounded blocks (including 32-bit rollover).
"""
from dataclasses import dataclass
import hashlib
import math
from pathlib import Path
import uuid

import numpy as np

MAX_BYTES = 256 * 1024 * 1024


from .errors import AnalysisError, fail


def fingerprint(path):
    before = path.stat()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
        fail("source_changed", path=str(path))
    return {"sha256": digest.hexdigest(), "size": after.st_size, "mtime_ns": after.st_mtime_ns}


def unit_factor(unit):
    unit = unit.decode() if isinstance(unit, bytes) else str(unit)
    factors = {"uV": 1., "µV": 1., "μV": 1., "mV": 1000., "V": 1e6}
    if unit not in factors:
        fail("unknown_unit", unit=unit)
    return factors[unit]


def positive_integer(value):
    return isinstance(value, int) and not isinstance(value, bool) and value > 0


class IntanSource:
    def __init__(self, path):
        from neo.rawio import IntanRawIO

        class BoundedIntanRawIO(IntanRawIO):
            def _assert_timestamp_continuity(self):
                # Traditional attached files only: never flatten the full timestamp field.
                if self.file_format != "header-attached":
                    fail("unsupported_source", path=str(path), reason="header-attached Intan required")
                timestamps = self._raw_data["timestamp"]
                previous = None
                for start in range(0, timestamps.shape[0], 4096):
                    part = np.asarray(timestamps[start:start + 4096]).reshape(-1)
                    ticks = part.astype(np.uint32, copy=False)
                    if previous is not None and int((int(ticks[0]) - previous) % (2**32)) != 1:
                        fail("discontinuous_time", path=str(path))
                    if len(ticks) > 1 and np.any(np.diff(ticks) != 1):
                        fail("discontinuous_time", path=str(path))
                    if len(ticks):
                        previous = int(ticks[-1])

        if path.name.lower() in ("info.rhd", "info.rhs"):
            fail("unsupported_source", path=str(path), reason="headerless recordings require a multi-file fingerprint")
        self.raw = BoundedIntanRawIO(filename=str(path))
        self.raw.parse_header()
        streams = self.raw.header["signal_streams"]
        indexes = np.flatnonzero(streams["id"] == "0")
        if len(indexes) != 1:
            fail("source_invalid", path=str(path), reason="missing amplifier stream")
        self.stream = int(indexes[0])
        channels = self.raw.header["signal_channels"]
        channels = channels[channels["stream_id"] == "0"]
        self.channels = channels
        self.saturation = None
        self.names = [str(ch["name"]) for ch in channels]
        self.factors = np.array([unit_factor(ch["units"]) for ch in channels])
        self.fs = float(self.raw.get_signal_sampling_rate(stream_index=self.stream))
        self.n_samples = int(self.raw.get_signal_size(0, 0, stream_index=self.stream))
        self.format = "Intan " + path.suffix[1:].upper()
        info = self.raw._global_info
        self.hardware = {"reference": info.get("reference_channel"),
                         "lower_bandwidth_hz": info.get("actual_lower_bandwidth"),
                         "upper_bandwidth_hz": info.get("actual_upper_bandwidth"),
                         "notch_filter_mode": info.get("notch_filter_mode")}
        self.hardware = {key: value.item() if isinstance(value, np.generic) else value
                         for key, value in self.hardware.items()}
        self.time_axis = "verified-continuous-int32-ticks"

    def read(self, start, count, channels):
        raw = self.raw.get_analogsignal_chunk(i_start=start, i_stop=start + count,
                                             stream_index=self.stream, channel_indexes=channels)
        # Inspect ADC codes before rescaling; actual integer dtype defines rails.
        if not np.issubdtype(raw.dtype, np.integer):
            fail("source_invalid", path=str(self.raw.filename), reason="amplifier ADC codes are not integer")
        limits = np.iinfo(raw.dtype)
        clipped = np.count_nonzero((raw == limits.min) | (raw == limits.max), axis=0)
        details = []
        for position, channel in enumerate(channels):
            metadata = self.channels[channel]
            rail_samples = int(clipped[position])
            if rail_samples > count // 1000:
                fail("adc_saturation", channel_id=str(channel), rail_samples=rail_samples, n_samples=count)
            gain, offset = float(metadata["gain"]), float(metadata["offset"])
            details.append({"channel_id": str(channel), "rail_samples": rail_samples,
                            "fraction": rail_samples / count, "raw_dtype": str(raw.dtype),
                            "raw_rails": [int(limits.min), int(limits.max)],
                            "rails_uV": [(limits.min * gain + offset) * self.factors[channel],
                                         (limits.max * gain + offset) * self.factors[channel]]})
        self.saturation = {"threshold_fraction": 0.001, "channels": details}
        scaled = self.raw.rescale_signal_raw_to_float(raw, dtype="float64", stream_index=self.stream,
                                                     channel_indexes=channels)
        return np.ascontiguousarray(scaled.T * self.factors[channels, None], dtype="<f8")


class Hdf5Source:
    def __init__(self, path):
        import h5py
        self.path = path
        with h5py.File(path, "r") as source:
            if "eeg" not in source or len(source["eeg"].shape) != 2:
                fail("source_invalid", path=str(path), reason="required eeg[C,N] dataset")
            if source.get("eeg", getlink=True).__class__.__name__ != "HardLink":
                fail("unsupported_source", path=str(path), reason="external or soft dataset link")
            data = source["eeg"]
            if data.dtype.kind not in "iuf":
                fail("source_invalid", path=str(path), reason="real numeric eeg dataset required")
            if data.is_virtual or data.external or source.get("eeg", getlink=True).__class__.__name__ != "HardLink":
                fail("unsupported_source", path=str(path), reason="external or virtual dataset")
            self.fs = float(data.attrs.get("fs", source.attrs.get("fs", math.nan)))
            original_unit=data.attrs.get("unit",source.attrs.get("unit","unknown"))
            original_unit=original_unit.decode() if isinstance(original_unit,bytes) else str(original_unit)
            if original_unit in ("uV","µV","μV","mV","V"):
                self.factor=unit_factor(original_unit);self.unit="uV"
            elif original_unit in ("count","1","degC","A","mA","Pa","kPa"):
                self.factor=1.;self.unit=original_unit
            else: fail("unknown_unit",unit=original_unit)
            if "channel_units" in data.attrs:
                units=[x.decode() if isinstance(x,bytes) else str(x) for x in data.attrs["channel_units"]]
                if any(u!=original_unit for u in units):fail("unsupported_source",path=str(path),reason="separation requires compatible channel units")
            names = data.attrs.get("ch_names")
            if names is None:
                fail("source_invalid", path=str(path), reason="explicit ch_names required")
            self.names = [x.decode() if isinstance(x, bytes) else str(x) for x in names]
            self.n_samples = int(data.shape[1])
            if len(self.names) != data.shape[0]:
                fail("source_invalid", path=str(path), reason="channel shape mismatch")
            # This schema defines a regular axis by fs; irregular timestamp datasets are rejected.
            if any(key in source for key in ("timestamps", "time", "time_s")):
                fail("unsupported_source", path=str(path), reason="explicit timestamp dataset is not supported")
        self.format, self.hardware = "HDF5 eeg[C,N]", {}
        self.source_metadata = {}
        with h5py.File(path,"r") as source:
            if "boundfree_signal_provenance" in source.attrs:
                import json
                self.source_metadata["signal_provenance"]=json.loads(source.attrs["boundfree_signal_provenance"])
        self.time_axis = "regular-by-schema"

    def read(self, start, count, channels):
        import h5py
        with h5py.File(self.path, "r") as source:
            # h5py fancy indexing sorts indexes; individual slices preserve requested order.
            return np.ascontiguousarray([source["eeg"][index, start:start + count]
                                         for index in channels], dtype="<f8") * self.factor


@dataclass
class SourceSnapshot:
    path: Path
    reader: object
    fingerprint: dict


class SourceRegistry:
    def __init__(self):
        self.sources = {}

    def open(self, params):
        value = params.get("path")
        if not isinstance(value, str) or not value:
            fail("invalid_request", field="path", reason="required string")
        path = Path(value).resolve()
        if not path.is_file():
            fail("source_invalid", path=str(path), reason="not a regular file")
        suffix = path.suffix.lower()
        from .tabular_sources import CsvSource, EdfSource
        source_type = {".csv": CsvSource, ".edf": EdfSource,
                       ".rhd": IntanSource, ".rhs": IntanSource,
                       ".h5": Hdf5Source, ".hdf5": Hdf5Source}.get(suffix)
        if source_type is None:
            fail("unsupported_source", path=str(path), reason="analysis supports attached RHD/RHS, explicit HDF5 eeg schema, regular CSV and continuous EDF")
        before = fingerprint(path)
        try:
            reader = CsvSource(path, params.get("csv_unit")) if source_type is CsvSource else source_type(path)
        except ImportError as exc:
            fail("unsupported_source", path=str(path), reason="dependency missing: " + str(exc.name))
        except AnalysisError:
            raise
        except Exception as exc:
            fail("source_invalid", path=str(path), reason=str(exc))
        if not math.isfinite(reader.fs) or reader.fs <= 0 or reader.n_samples < 1 or not reader.names:
            fail("source_invalid", path=str(path), reason="invalid sampling metadata")
        if any(not name for name in reader.names):
            fail("source_invalid", path=str(path), reason="empty channel name")
        if fingerprint(path) != before:
            fail("source_changed", path=str(path))
        handle = uuid.uuid4().hex
        self.sources[handle] = SourceSnapshot(path, reader, before)
        return {"handle": handle, "path": str(path), "fs": reader.fs, "n_samples": reader.n_samples,
                "n_channels": len(reader.names), "duration": reader.n_samples / reader.fs,
                "ch_names": reader.names, "channel_ids": [str(i) for i in range(len(reader.names))],
                "fingerprint": before, "format": reader.format, "unit": getattr(reader,"unit","uV"),
                "hardware": {"reference": None, "lower_bandwidth_hz": None, "upper_bandwidth_hz": None,
                             "notch_filter_mode": None, **reader.hardware}, "time_axis": reader.time_axis,
                "capabilities": ["integer-slice", "float64-le", "source-sha256"],
                **getattr(reader, "source_metadata", {})}

    def get(self, params):
        handle = params.get("handle")
        if not isinstance(handle, str) or handle not in self.sources:
            fail("handle_not_found", handle=str(handle))
        return self.sources[handle]

    def read(self, params):
        snapshot = self.get(params)
        reader = snapshot.reader
        start, count, ids = params.get("first_sample"), params.get("n_samples"), params.get("channel_ids")
        if (not isinstance(start, int) or isinstance(start, bool) or start < 0 or not positive_integer(count)
                or start + count > reader.n_samples):
            fail("invalid_request", field="sample_range", reason="integer range outside source")
        valid_ids = {str(i) for i in range(len(reader.names))}
        if (not isinstance(ids, list) or not ids or len(ids) > 40 or any(not isinstance(x, str) or x not in valid_ids for x in ids)
                or len(set(ids)) != len(ids)):
            fail("invalid_request", field="channel_ids", reason="required unique valid channel IDs, at most 40")
        size = len(ids) * count * 8
        if size > MAX_BYTES:
            fail("resource_limit", bytes=size, limit_bytes=MAX_BYTES)
        if fingerprint(snapshot.path) != snapshot.fingerprint:
            fail("source_changed", path=str(snapshot.path))
        try:
            data = np.ascontiguousarray(reader.read(start, count, [int(x) for x in ids]), dtype="<f8")
        except AnalysisError:
            raise
        except Exception as exc:
            fail("source_invalid", path=str(snapshot.path), reason=str(exc))
        if fingerprint(snapshot.path) != snapshot.fingerprint:
            fail("source_changed", path=str(snapshot.path))
        if data.shape != (len(ids), count) or not np.isfinite(data).all():
            fail("source_invalid", path=str(snapshot.path), reason="invalid or non-finite signal samples")
        return {"handle": params["handle"], "shape": [len(ids), count], "dtype": "float64-le", "unit": getattr(reader,"unit","uV"),
                "fs": reader.fs, "first_sample": start, "channel_ids": ids,
                "fingerprint": snapshot.fingerprint, "payload_bytes": size, "data": data,
                "saturation": getattr(reader, "saturation", None)}

    def close(self, params):
        self.get(params)
        del self.sources[params["handle"]]
        return {"closed": True}
