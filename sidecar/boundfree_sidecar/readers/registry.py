from pathlib import Path

from .base import Reader
from .csv_reader import CsvReader
from .hdf5_reader import Hdf5Reader


def reader_for_path(path: Path) -> Reader | None:
    if path.suffix.lower() == ".csv":
        return CsvReader(path)
    if path.suffix.lower() in {".h5", ".hdf5"}:
        return Hdf5Reader(path)
    return None
