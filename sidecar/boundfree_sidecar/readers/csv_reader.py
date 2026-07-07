import csv
from pathlib import Path

from .base import FileMetadata, Reader, SliceData


class CsvReader(Reader):
    format_name = "CSV"

    def __init__(self, path: Path) -> None:
        super().__init__(path)
        self._rows: list[dict[str, str]] | None = None

    def _load_rows(self) -> list[dict[str, str]]:
        if self._rows is None:
            with self.path.open("r", encoding="utf-8", newline="") as f:
                self._rows = list(csv.DictReader(f))
        return self._rows

    def _load_events(self) -> list[dict[str, object]]:
        markers_path = self.path.with_name(self.path.stem + "_markers.csv")
        if not markers_path.exists():
            return []
        events: list[dict[str, object]] = []
        with markers_path.open("r", encoding="utf-8", newline="") as f:
            for row in csv.DictReader(f):
                events.append({"time": float(row["time_s"]), "label": str(row["label"])})
        return events

    def metadata(self) -> FileMetadata:
        rows = self._load_rows()
        if not rows:
            raise ValueError("CSV contains no samples")
        ch_names = [name for name in rows[0].keys() if name != "time_s"]
        times = [float(row["time_s"]) for row in rows]
        fs = 1.0 / (times[1] - times[0]) if len(times) > 1 else 1.0
        duration = len(rows) / fs
        return FileMetadata(
            fs=fs,
            n_channels=len(ch_names),
            n_samples=len(rows),
            duration=duration,
            ch_names=ch_names,
            unit="uV",
            format_name=self.format_name,
            events=self._load_events(),
        )

    def get_slice(self, t0: float, t1: float) -> SliceData:
        rows = self._load_rows()
        meta = self.metadata()
        start = max(0, int(round(t0 * meta.fs)))
        stop = min(meta.n_samples, int(round(t1 * meta.fs)))
        return SliceData(
            t0=t0,
            t1=t1,
            fs=meta.fs,
            unit="uV",
            data=[
                [float(row[ch]) for row in rows[start:stop]]
                for ch in meta.ch_names
            ],
        )
