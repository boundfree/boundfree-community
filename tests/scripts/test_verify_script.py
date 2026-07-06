import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_m1_foundation_verify_script_exists_and_supports_fast_mode():
    script = ROOT / "scripts" / "verify_m1_foundation.sh"
    assert script.exists()

    result = subprocess.run(
        [str(script), "--fast"],
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
