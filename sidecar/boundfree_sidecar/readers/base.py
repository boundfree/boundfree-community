from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class FileMetadata:
    fs: float
    n_channels: int
    n_samples: int
    duration: float
    ch_names: list[str]
    unit: str
    format_name: str
    events: list[dict[str, object]]


class Reader:
    format_name = ""

    def __init__(self, path: Path) -> None:
        self.path = path

    def metadata(self) -> FileMetadata:
        raise NotImplementedError

    def get_slice(self, t0: float, t1: float) -> list[list[float]]:
        raise NotImplementedError
