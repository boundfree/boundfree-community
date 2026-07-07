from pathlib import Path

from .base import Reader
from .csv_reader import CsvReader


def reader_for_path(path: Path) -> Reader | None:
    if path.suffix.lower() == ".csv":
        return CsvReader(path)
    return None
