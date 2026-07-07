from pathlib import Path

from .base import Reader
from .csv_reader import CsvReader
from .hdf5_reader import Hdf5Reader
from .mne_reader import MneReader


def reader_for_path(path: Path) -> Reader | None:
    if path.suffix.lower() == ".csv":
        return CsvReader(path)
    if path.suffix.lower() in {".h5", ".hdf5"}:
        return Hdf5Reader(path)
    if path.suffix.lower() in {".edf", ".bdf", ".vhdr", ".set", ".fif"}:
        return MneReader(path)
    return None
