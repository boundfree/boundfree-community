import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


def _usable_bash() -> str | None:
    bash = shutil.which("bash")
    if bash is None:
        return None
    # Windows ships a WSL interop bash.exe under System32. It is not a usable
    # host shell for running repository scripts with Windows paths.
    if sys.platform == "win32":
        normalized = bash.replace("/", "\\").lower()
        if normalized.endswith("\\system32\\bash.exe") or normalized.endswith("\\sysnative\\bash.exe"):
            return None
    return bash


def test_m1_foundation_verify_script_exists_and_supports_fast_mode():
    script = ROOT / "scripts" / "wsl" / "verify_m1_foundation.sh"
    assert script.exists()

    bash = _usable_bash()
    if bash is None:
        pytest.skip("usable bash is unavailable on this platform")

    result = subprocess.run(
        [bash, str(script), "--fast"],
        cwd=ROOT,
        env={
            **os.environ,
            "PYTHONPATH": str(ROOT / "sidecar"),
            "QT_QPA_PLATFORM": "offscreen",
        },
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )
    assert "M1 foundation verification passed" in result.stdout
