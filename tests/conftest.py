from pathlib import Path
import sys


def pytest_configure(config):
    root = Path(__file__).resolve().parents[1]
    sidecar = root / "sidecar"
    if str(sidecar) not in sys.path:
        sys.path.insert(0, str(sidecar))
