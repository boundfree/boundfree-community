import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_i18n_static_check_passes():
    subprocess.run(
        [sys.executable, "scripts/check_i18n.py"],
        cwd=ROOT,
        text=True,
        check=True,
    )
