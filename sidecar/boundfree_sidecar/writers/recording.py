import math
import queue
import threading
from dataclasses import dataclass, field
from datetime import datetime, timezone
from itertools import count
from pathlib import Path
from typing import Any


_STOP = object()


@dataclass(frozen=True)
class RecordingStats:
    backlog_chunks: int
    samples_written: int
    dropped_chunks: int
    dropped_events: int


@dataclass
class RecordingSession:
    handle: str
    fs: float
    ch_names: list[str]
    output_dir: Path
    meta: dict[str, str]
    formats: tuple[str, ...]
    max_queue_chunks: int
    paths: dict[str, Path]
    _queue: queue.Queue[Any] = field(init=False)
    _thread: threading.Thread | None = field(default=None, init=False)
    _accepting: bool = field(default=False, init=False)
    _samples_written: int = field(default=0, init=False)
    _dropped_chunks: int = field(default=0, init=False)
    _dropped_events: int = field(default=0, init=False)
    _failure: BaseException | None = field(default=None, init=False)
    _ready: threading.Event = field(default_factory=threading.Event, init=False)

    def __post_init__(self) -> None:
        self._queue = queue.Queue(maxsize=self.max_queue_chunks)

    @property
    def n_channels(self) -> int:
        return len(self.ch_names)

    def start(self) -> None:
        self._accepting = True
        self._thread = threading.Thread(target=self._worker, name=f"record-{self.handle}", daemon=True)
        self._thread.start()
        if not self._ready.wait(timeout=10.0):
            self._accepting = False
            raise TimeoutError("recording writer initialization timed out")
        self.raise_if_failed()

    def raise_if_failed(self) -> None:
        if self._failure is not None:
            raise RuntimeError(str(self._failure)) from self._failure

    def stats(self) -> RecordingStats:
        return RecordingStats(
            backlog_chunks=self._queue.qsize(),
            samples_written=self._samples_written,
            dropped_chunks=self._dropped_chunks,
            dropped_events=self._dropped_events,
        )

    def write(self, payload: bytes, shape: tuple[int, int]) -> bool:
        import numpy as np

        self.raise_if_failed()
        if not self._accepting:
            raise RuntimeError("recording is not accepting data")
        n_channels, n_samples = shape
        expected_bytes = n_channels * n_samples * 4
        if n_channels != self.n_channels or n_samples <= 0 or len(payload) != expected_bytes:
            raise ValueError("invalid recording chunk shape or payload length")
        data = np.frombuffer(payload, dtype="<f4").reshape(shape)
        if not np.isfinite(data).all():
            raise ValueError("recording chunk contains non-finite samples")
        try:
            self._queue.put_nowait(("data", data.copy()))
            return True
        except queue.Full:
            self._dropped_chunks += 1
            return False

    def add_event(self, time_seconds: float, label: str) -> bool:
        self.raise_if_failed()
        if not self._accepting:
            raise RuntimeError("recording is not accepting events")
        if not math.isfinite(time_seconds) or time_seconds < 0.0:
            raise ValueError("event time must be finite and non-negative")
        try:
            self._queue.put_nowait(("event", float(time_seconds), label))
            return True
        except queue.Full:
            self._dropped_events += 1
            return False

    def close(self, timeout: float = 10.0) -> RecordingStats:
        self._accepting = False
        self.raise_if_failed()
        try:
            self._queue.put(_STOP, timeout=timeout)
        except queue.Full as exc:
            raise TimeoutError("recording queue did not accept close marker") from exc
        if self._thread is not None:
            self._thread.join(timeout=timeout)
            if self._thread.is_alive():
                raise TimeoutError("recording queue did not drain before timeout")
        self.raise_if_failed()
        return self.stats()

    def _worker(self) -> None:
        edf_writer = None
        h5_file = None
        h5_eeg = None
        h5_times = None
        h5_labels = None
        try:
            import numpy as np

            if "edf" in self.formats:
                import pyedflib

                edf_writer = pyedflib.EdfWriter(str(self.paths["edf"]), self.n_channels)
                edf_writer.setSignalHeaders(
                    [
                        {
                            "label": name[:16],
                            "dimension": "uV",
                            "sample_frequency": self.fs,
                            "physical_min": -3200.0,
                            "physical_max": 3200.0,
                            "digital_min": -32768,
                            "digital_max": 32767,
                            "prefilter": "",
                            "transducer": "",
                        }
                        for name in self.ch_names
                    ]
                )
                edf_writer.setPatientCode(self.meta.get("animal_id", ""))
                edf_writer.setRecordingAdditional(self.meta.get("notes", ""))
            if "hdf5" in self.formats:
                import h5py

                h5_file = h5py.File(self.paths["hdf5"], "w")
                chunk_samples = max(1, int(round(self.fs)))
                h5_eeg = h5_file.create_dataset(
                    "eeg",
                    shape=(self.n_channels, 0),
                    maxshape=(self.n_channels, None),
                    chunks=(self.n_channels, chunk_samples),
                    dtype="float32",
                )
                if self.meta.get("signal_provenance"):
                    import json
                    provenance=json.loads(self.meta["signal_provenance"])
                    if not isinstance(provenance,dict):raise ValueError("invalid recording signal provenance")
                    h5_file.attrs["boundfree_signal_provenance"]=json.dumps(provenance,ensure_ascii=False,allow_nan=False)
                h5_eeg.attrs["fs"] = self.fs
                h5_eeg.attrs["ch_names"] = np.asarray(self.ch_names, dtype=h5py.string_dtype("utf-8"))
                h5_eeg.attrs["unit"] = self.meta.get("signal_unit","uV")
                if self.meta.get("signal_channel_units"):
                    import json
                    units=json.loads(self.meta["signal_channel_units"])
                    if len(units)!=self.n_channels:raise ValueError("invalid recorded channel units")
                    h5_eeg.attrs["channel_units"]=np.asarray(units,dtype=h5py.string_dtype("utf-8"))
                metadata = h5_file.require_group("meta")
                for key, value in self.meta.items():
                    metadata.attrs[key] = value
                metadata.attrs["start_time"] = datetime.now(timezone.utc).isoformat()
                events = h5_file.require_group("events")
                h5_times = events.create_dataset("time", shape=(0,), maxshape=(None,), dtype="float64")
                h5_labels = events.create_dataset(
                    "label", shape=(0,), maxshape=(None,), dtype=h5py.string_dtype("utf-8")
                )
            self._ready.set()
            while True:
                item = self._queue.get()
                if item is _STOP:
                    break
                if item[0] == "data":
                    data = item[1]
                    if edf_writer is not None:
                        edf_writer.writeSamples([data[index] for index in range(self.n_channels)])
                    if h5_eeg is not None:
                        old_samples = h5_eeg.shape[1]
                        h5_eeg.resize(old_samples + data.shape[1], axis=1)
                        h5_eeg[:, old_samples:] = data
                    self._samples_written += int(data.shape[1])
                elif h5_times is not None and h5_labels is not None:
                    old_events = h5_times.shape[0]
                    h5_times.resize(old_events + 1, axis=0)
                    h5_labels.resize(old_events + 1, axis=0)
                    h5_times[old_events] = item[1]
                    h5_labels[old_events] = item[2]
        except BaseException as exc:  # Writer failure must remain observable through protocol calls.
            self._failure = exc
            self._accepting = False
            self._ready.set()
        finally:
            if edf_writer is not None:
                edf_writer.close()
            if h5_file is not None:
                h5_file.close()


class RecordingRegistry:
    def __init__(self) -> None:
        self._next_id = count(1)
        self._sessions: dict[str, RecordingSession] = {}

    def open(
        self,
        fs: float,
        ch_names: list[str],
        output_dir: Path,
        meta: dict[str, str],
        formats: tuple[str, ...] = ("edf", "hdf5"),
        max_queue_chunks: int = 256,
    ) -> RecordingSession:
        output_dir.mkdir(parents=True, exist_ok=True)
        if not output_dir.is_dir():
            raise OSError("recording output path is not a directory")
        handle = f"record-{next(self._next_id)}"
        animal = meta.get("animal_id", "unknown").strip().replace(" ", "_") or "unknown"
        stem = f"{animal}_{datetime.now().strftime('%Y%m%d_%H%M%S_%f')}"
        paths: dict[str, Path] = {}
        if "edf" in formats:
            paths["edf"] = output_dir / f"{stem}.edf"
        if "hdf5" in formats:
            paths["hdf5"] = output_dir / f"{stem}.h5"
        session = RecordingSession(handle, fs, ch_names, output_dir, meta, formats, max_queue_chunks, paths)
        session.start()
        self._sessions[handle] = session
        return session

    def get(self, handle: str) -> RecordingSession | None:
        return self._sessions.get(handle)

    def close(self, handle: str, timeout: float = 10.0) -> tuple[RecordingSession, RecordingStats] | None:
        session = self._sessions.get(handle)
        if session is None:
            return None
        stats = session.close(timeout)
        del self._sessions[handle]
        return session, stats
