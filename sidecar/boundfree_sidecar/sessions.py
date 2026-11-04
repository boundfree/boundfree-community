from dataclasses import dataclass
from itertools import count
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class FileSession:
    handle: str
    path: Path
    fs: float
    n_channels: int
    n_samples: int
    duration: float
    ch_names: list[str]
    reader: Any | None = None


class FileSessionRegistry:
    def __init__(self) -> None:
        self._next_id = count(1)
        self._sessions: dict[str, FileSession] = {}

    def add(
        self,
        path: Path,
        fs: float,
        n_channels: int,
        n_samples: int,
        ch_names: list[str],
        reader: Any | None = None,
        duration: float | None = None,
    ) -> FileSession:
        handle = f"file-{next(self._next_id)}"
        duration = duration if duration is not None else n_samples / fs if fs > 0 else 0.0
        session = FileSession(
            handle=handle,
            path=path,
            fs=fs,
            n_channels=n_channels,
            n_samples=n_samples,
            duration=duration,
            ch_names=ch_names,
            reader=reader,
        )
        self._sessions[handle] = session
        return session

    def get(self, handle: str) -> FileSession | None:
        return self._sessions.get(handle)

    def remove(self, handle: str) -> bool:
        return self._sessions.pop(handle, None) is not None
