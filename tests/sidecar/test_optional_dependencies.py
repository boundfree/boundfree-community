try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python 3.10 fallback
    import tomli as tomllib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_sidecar_declares_format_optional_dependencies():
    data = tomllib.loads((ROOT / "sidecar" / "pyproject.toml").read_text(encoding="utf-8"))

    assert data["project"]["optional-dependencies"]["formats"] == [
        "h5py==3.16.0",
        "mne==1.12.1",
        "pyedflib==0.1.42",
    ]
