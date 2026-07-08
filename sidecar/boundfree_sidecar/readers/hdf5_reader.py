from pathlib import Path

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

    def __init__(self, path: Path, dataset_path: str = "eeg") -> None:
        super().__init__(path)
        self.dataset_path = dataset_path

    def metadata(self) -> FileMetadata:
        import h5py

        with h5py.File(self.path, "r") as f:
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

        return FileMetadata(
            fs=fs,
            n_channels=n_channels,
            n_samples=n_samples,
            duration=n_samples / fs if fs > 0 else 0.0,
            ch_names=ch_names,
            unit=unit,
            format_name=self.format_name,
            events=[],
        )

    def get_slice(self, t0: float, t1: float) -> SliceData:
        import h5py

        meta = self.metadata()
        start = max(0, int(round(t0 * meta.fs)))
        stop = min(meta.n_samples, int(round(t1 * meta.fs)))

        with h5py.File(self.path, "r") as f:
            data = f[self.dataset_path][:, start:stop]

        return SliceData(
            t0=t0,
            t1=t1,
            fs=meta.fs,
            unit=meta.unit,
            data=data.astype(float).tolist(),
        )
