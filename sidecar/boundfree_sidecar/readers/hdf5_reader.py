from pathlib import Path
import math
import json

from .base import FileMetadata, Reader, SliceData


def _normalize_channel_names(raw_names: object, n_channels: int) -> list[str]:
    if isinstance(raw_names, bytes):
        names = [raw_names.decode()]
    elif isinstance(raw_names, str):
        names = [raw_names]
    else:
        names = [name.decode() if isinstance(name, bytes) else str(name) for name in raw_names]

    if len(names) != n_channels:
        raise ValueError(f"channel name count {len(names)} does not match n_channels {n_channels}")
    return names


class Hdf5Reader(Reader):
    format_name = "HDF5"

    def __init__(self, path: Path, dataset_path: str = "eeg", capture_stream: str = "") -> None:
        super().__init__(path)
        self.dataset_path = dataset_path
        self.capture_stream = capture_stream
        self.capture_reader = None

    def metadata(self) -> FileMetadata:
        import h5py

        if self.capture_reader is not None:
            return self.capture_reader.metadata()

        with h5py.File(self.path, "r") as f:
            if self.dataset_path not in f and "streams" in f:
                from .capture_reader import CaptureReader
                self.capture_reader = CaptureReader(self.path, self.capture_stream)
                return self.capture_reader.metadata()
            if self.dataset_path not in f:
                raise KeyError(f"missing dataset {self.dataset_path}")

            ds = f[self.dataset_path]
            if len(ds.shape) != 2:
                raise ValueError(f"dataset {self.dataset_path} must be 2D")

            n_channels = int(ds.shape[0])
            n_samples = int(ds.shape[1])
            fs = float(ds.attrs.get("fs", f.attrs.get("fs", 256.0)))
            raw_names = ds.attrs.get("ch_names", [f"Ch{i + 1}" for i in range(n_channels)])
            ch_names = _normalize_channel_names(raw_names, n_channels)
            unit = str(ds.attrs.get("unit", f.attrs.get("unit", "uV")))

            self.source_metadata=None
            if "boundfree_signal_provenance" in f.attrs:
                payload=str(f.attrs["boundfree_signal_provenance"])
                if len(payload)>1024*1024:raise ValueError("signal provenance exceeds 1 MiB")
                self.source_metadata=json.loads(payload)
                if not isinstance(self.source_metadata,dict):raise ValueError("invalid signal provenance")
            self.channel_units = None
            self.sample_times = None
            if "channel_units" in ds.attrs:
                self.channel_units = _normalize_channel_names(ds.attrs["channel_units"],n_channels)
            if "sample_times" in f:
                import numpy as np
                axis=f["sample_times"]
                if axis.ndim!=1 or axis.shape[0]!=n_samples or n_samples>4_000_000:
                    raise ValueError("invalid signal time coordinates")
                self.sample_times=np.asarray(axis[:],dtype=np.float64)
                if not np.isfinite(self.sample_times).all() or np.any(np.diff(self.sample_times)<0):
                    raise ValueError("invalid signal time coordinates")
            events: list[dict[str, object]] = []
            if "events/time" in f and "events/label" in f:
                raw_times = f["events/time"][:]
                raw_labels = f["events/label"][:]
                if len(raw_times) != len(raw_labels):
                    raise ValueError("events/time and events/label lengths differ")
                for raw_time, raw_label in zip(raw_times, raw_labels, strict=False):
                    time = float(raw_time)
                    if not math.isfinite(time) or time < 0.0:
                        raise ValueError("event time must be finite and non-negative")
                    label = raw_label.decode() if isinstance(raw_label, bytes) else str(raw_label)
                    events.append({"time": time, "label": label})
        return FileMetadata(
            fs=fs,
            n_channels=n_channels,
            n_samples=n_samples,
            duration=float(self.sample_times[-1]+1/fs) if self.sample_times is not None and len(self.sample_times) else n_samples / fs if fs > 0 else 0.0,
            ch_names=ch_names,
            unit=unit,
            format_name=self.format_name,
            events=events,
            channel_units=self.channel_units,
            source_metadata=self.source_metadata,
        )

    def get_slice(self, t0: float, t1: float) -> SliceData:
        import h5py

        meta = self.metadata()
        if self.capture_reader is not None:
            return self.capture_reader.get_slice(t0, t1)
        start = max(0, int(round(t0 * meta.fs)))
        stop = min(meta.n_samples, int(round(t1 * meta.fs)))

        if self.sample_times is not None:
            import numpy as np
            start=int(np.searchsorted(self.sample_times,t0,"left"));stop=int(np.searchsorted(self.sample_times,t1,"left"))
        with h5py.File(self.path, "r") as f:
            data = f[self.dataset_path][:, start:stop]

        return SliceData(
            t0=t0,
            t1=t1,
            fs=meta.fs,
            unit=meta.unit,
            data=data.astype(float).tolist(),
            channel_units=self.channel_units,
            sample_times=None if self.sample_times is None else self.sample_times[start:stop].tolist(),
        )
