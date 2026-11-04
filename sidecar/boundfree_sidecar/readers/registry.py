from pathlib import Path

from .base import Reader
from .csv_reader import CsvReader
from .hdf5_reader import Hdf5Reader
from .intan_reader import IntanReader
from .mne_reader import MneReader
from .capture_reader import CaptureReader


def reader_for_path(path: Path, capture_stream: str = "") -> Reader | None:
    if path.is_dir() or path.name.lower() == "manifest.json":
        return CaptureReader(path, capture_stream)
    if path.suffix.lower() == ".csv":
        return CsvReader(path)
    if path.suffix.lower() in {".h5", ".hdf5"}:
        if path.name.lower() == "signals.h5" and (path.parent / "manifest.json").is_file():
            return CaptureReader(path, capture_stream)
        return Hdf5Reader(path, capture_stream=capture_stream)
    if path.suffix.lower() in {".edf", ".bdf", ".vhdr", ".set", ".fif"}:
        return MneReader(path)
    if path.suffix.lower() in {".rhd", ".rhs"}:
        return IntanReader(path)
    return None
