from pathlib import Path

from .base import FileMetadata, Reader, SliceData

class MneReader(Reader):
    def __init__(self, path: Path) -> None:
        super().__init__(path)
        self._raw = None
        self._metadata: FileMetadata | None = None

    @property
    def format_name(self) -> str:
        suffix = self.path.suffix.lower()
        if suffix == ".edf":
            return "EDF"
        if suffix == ".bdf":
            return "BDF"
        if suffix == ".vhdr":
            return "BrainVision"
        if suffix == ".set":
            return "EEGLAB"
        if suffix == ".fif":
            return "MNE FIF"
        return "MNE"

    def _load_raw(self):
        if self._raw is None:
            import mne

            mne.set_log_level("WARNING")
            self._raw = mne.io.read_raw(self.path, preload=False, verbose=False)
        return self._raw

    def metadata(self) -> FileMetadata:
        if self._metadata is not None:
            return self._metadata

        raw = self._load_raw()
        fs = float(raw.info["sfreq"])
        self._metadata = FileMetadata(
            fs=fs,
            n_channels=len(raw.ch_names),
            n_samples=int(raw.n_times),
            duration=float(raw.n_times) / fs if fs > 0 else 0.0,
            ch_names=list(raw.ch_names),
            unit="uV",
            format_name=self.format_name,
            events=[
                {"time": round(float(onset), 6), "label": str(description)}
                for onset, description in zip(raw.annotations.onset, raw.annotations.description, strict=False)
                if str(description)
            ],
        )
        return self._metadata

    def get_slice(self, t0: float, t1: float) -> SliceData:
        meta = self.metadata()
        start = max(0, int(round(t0 * meta.fs)))
        stop = min(meta.n_samples, int(round(t1 * meta.fs)))
        data_v, _ = self._load_raw()[:, start:stop]
        return SliceData(
            t0=t0,
            t1=t1,
            fs=meta.fs,
            unit="uV",
            data=(data_v * 1e6).astype(float).tolist(),
        )
