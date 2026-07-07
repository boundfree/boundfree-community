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


@dataclass(frozen=True)
class SliceData:
    t0: float
    t1: float
    fs: float
    unit: str
    data: list[list[float]]

    @property
    def shape(self) -> list[int]:
        return [len(self.data), len(self.data[0]) if self.data else 0]


class Reader:
    format_name = ""

    def __init__(self, path: Path) -> None:
        self.path = path

    def metadata(self) -> FileMetadata:
        raise NotImplementedError

    def get_slice(self, t0: float, t1: float) -> SliceData:
        raise NotImplementedError
