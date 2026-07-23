import json
from dataclasses import dataclass
import os
import sys
from pathlib import Path


@dataclass(frozen=True)
class VersionInfo:
    schema_version: int
    product_name: str
    version: str
    release_channel: str
    ipc_protocol_version: int


def repo_root() -> Path:
    configured = os.environ.get("BF_RESOURCE_ROOT")
    if configured:
        return Path(configured)
    if getattr(sys, "frozen", False):
        candidates = [
            Path(getattr(sys, "_MEIPASS", Path(sys.executable).resolve().parent)),
            Path(sys.executable).resolve().parent.parent,
            Path(sys.executable).resolve().parent,
        ]
        for candidate in candidates:
            if (candidate / "version.json").exists():
                return candidate
        return candidates[0]
    return Path(__file__).resolve().parents[2]

def load_version(path: Path | None = None) -> VersionInfo:
    version_path = path or repo_root() / "version.json"
    raw = json.loads(version_path.read_text(encoding="utf-8"))
    return VersionInfo(
        schema_version=int(raw["schema_version"]),
        product_name=str(raw["product_name"]),
        version=str(raw["version"]),
        release_channel=str(raw["release_channel"]),
        ipc_protocol_version=int(raw["ipc_protocol_version"]),
    )
