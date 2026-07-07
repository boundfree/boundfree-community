import csv
from pathlib import Path

from .base import FileMetadata, Reader, SliceData


class CsvReader(Reader):
    format_name = "CSV"

    def __init__(self, path: Path) -> None:
        super().__init__(path)
        self._rows: list[dict[str, str]] | None = None
        self._metadata: FileMetadata | None = None

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
        if self._metadata is not None:
            return self._metadata

        with self.path.open("r", encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f)
            first_row = next(reader, None)
            if first_row is None:
                raise ValueError("CSV contains no samples")

            ch_names = [name for name in first_row.keys() if name != "time_s"]
            first_time = float(first_row["time_s"])
            second_row = next(reader, None)
            second_time = float(second_row["time_s"]) if second_row is not None else None
            n_samples = 1 if second_row is None else 2

            for _ in reader:
                n_samples += 1

        fs = 1.0 / (second_time - first_time) if second_time is not None else 1.0
        duration = n_samples / fs
        self._metadata = FileMetadata(
            fs=fs,
            n_channels=len(ch_names),
            n_samples=n_samples,
            duration=duration,
            ch_names=ch_names,
            unit="uV",
            format_name=self.format_name,
            events=self._load_events(),
        )
        return self._metadata

    def get_slice(self, t0: float, t1: float) -> SliceData:
        meta = self.metadata()
        start = max(0, int(round(t0 * meta.fs)))
        stop = min(meta.n_samples, int(round(t1 * meta.fs)))
        data = [[] for _ in meta.ch_names]

        with self.path.open("r", encoding="utf-8", newline="") as f:
            for index, row in enumerate(csv.DictReader(f)):
                if index < start:
                    continue
                if index >= stop:
                    break
                for channel_index, channel_name in enumerate(meta.ch_names):
                    data[channel_index].append(float(row[channel_name]))

        return SliceData(
            t0=t0,
            t1=t1,
            fs=meta.fs,
            unit="uV",
            data=data,
        )
