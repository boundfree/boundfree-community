import csv
import math
import json
from pathlib import Path

from .base import FileMetadata, Reader, SliceData
from .errors import CsvSchemaError

class CsvReader(Reader):
    format_name = "CSV"

    def __init__(self, path: Path) -> None:
        super().__init__(path)
        self._rows: list[dict[str, str]] | None = None
        self._metadata: FileMetadata | None = None

    @staticmethod
    def _number(value: str | None) -> float | None:
        try:
            number = float(value) if value is not None else None
        except ValueError:
            return None
        return number if number is not None and math.isfinite(number) else None

    def _validated_csv(self) -> tuple[list[str], int, float, float | None]:
        with self.path.open("r", encoding="utf-8", newline="") as f:
            reader = csv.DictReader(f)
            headers = reader.fieldnames
            if headers is None or "time_s" not in headers:
                raise CsvSchemaError("csv.missing_time_column")

            channels = [header for header in headers if header and header != "time_s"]
            if not channels:
                raise CsvSchemaError("csv.no_signal_columns")

            if headers == ["time_s", "label"]:
                raise CsvSchemaError("csv.marker_table")
            n_samples = 0
            first_time = 0.0
            second_time: float | None = None
            previous_time: float | None = None
            for row_number, row in enumerate(reader, start=1):

                time = self._number(row.get("time_s"))
                if time is None or (previous_time is not None and time <= previous_time):
                    raise CsvSchemaError("csv.invalid_time_axis")
                previous_time = time
                if n_samples == 0:
                    first_time = time
                elif n_samples == 1:
                    second_time = time

                for channel in channels:
                    if self._number(row.get(channel)) is None:
                        raise CsvSchemaError(
                            "csv.invalid_signal_value", {"column": channel, "row": row_number})
                n_samples += 1

        if n_samples == 0:
            raise CsvSchemaError("csv.no_samples")
        return channels, n_samples, first_time, second_time

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

        ch_names, n_samples, first_time, second_time = self._validated_csv()

        fs = 1.0 / (second_time - first_time) if second_time is not None else 1.0
        duration = n_samples / fs
        self.channel_units = None; unit="uV"; self.source_metadata=None
        metadata_path=self.path.with_suffix(self.path.suffix+".meta.json")
        if metadata_path.is_file():
            with metadata_path.open("rb") as f: payload=f.read(1024*1024+1)
            if len(payload)>1024*1024:raise ValueError("CSV signal metadata exceeds 1 MiB")
            metadata=json.loads(payload.decode("utf-8"))
            if metadata.get("schema")!="boundfree.signal-export.v1":raise ValueError("unsupported CSV signal metadata")
            unit=metadata.get("unit");self.channel_units=metadata.get("channel_units");self.source_metadata=metadata.get("provenance")
            if not isinstance(unit,str) or not unit or not isinstance(self.channel_units,list) or len(self.channel_units)!=len(ch_names) or any(not isinstance(u,str) or not u for u in self.channel_units):
                raise ValueError("invalid CSV signal units")
        self._metadata = FileMetadata(
            fs=fs,
            n_channels=len(ch_names),
            n_samples=n_samples,
            duration=duration,
            ch_names=ch_names,
            unit=unit,
            channel_units=self.channel_units,
            source_metadata=self.source_metadata,
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
            channel_units=self.channel_units,
        )
