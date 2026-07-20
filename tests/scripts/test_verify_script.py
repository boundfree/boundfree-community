import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


def test_m1_foundation_verify_script_exists_and_supports_fast_mode():
    script = ROOT / "scripts" / "verify_m1_foundation.sh"
    assert script.exists()

    bash = shutil.which("bash")
    if bash is None:
        pytest.skip("bash is unavailable on this platform")

    result = subprocess.run(
        [bash, str(script), "--fast"],
        cwd=ROOT,
        env={
            **os.environ,
            "PYTHONPATH": str(ROOT / "sidecar"),
            "QT_QPA_PLATFORM": "offscreen",
        },
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    assert "M1 foundation verification passed" in result.stdout
