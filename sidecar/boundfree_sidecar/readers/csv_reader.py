import csv
from pathlib import Path

from .base import FileMetadata, Reader


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

    def metadata(self) -> FileMetadata:
        rows = self._load_rows()
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
            events=[],
        )

    def get_slice(self, t0: float, t1: float) -> list[list[float]]:
        rows = self._load_rows()
        meta = self.metadata()
        start = max(0, int(round(t0 * meta.fs)))
        stop = min(meta.n_samples, int(round(t1 * meta.fs)))
        return [
            [float(row[ch]) for row in rows[start:stop]]
            for ch in meta.ch_names
        ]
