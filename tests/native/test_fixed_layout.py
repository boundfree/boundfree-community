"""Fixed workspace geometry and reachable controls across supported locales."""

import os
from pathlib import Path
import subprocess

import pytest


def test_fixed_workspace_resize_panels_and_languages():
    root = Path(__file__).resolve().parents[2]
    candidates = [root / "native" / build / config / name
                  for build in ("build-windows", "build")
                  for config in ("Release", "Debug", "")
                  for name in ("bf_app.exe", "bf_app")]
    app = next((candidate for candidate in candidates if candidate.is_file()), None)
    if app is None:
        pytest.skip("请先构建 bf_app")
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    result = subprocess.run([str(app), "--smoke-ui-fixed-layout"], cwd=root, env=env,
                            capture_output=True, text=True, encoding="utf-8",
                            errors="replace", timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "smoke-ui-fixed-layout-ok" in result.stdout
    assert "locales=4" in result.stdout
    assert "panels=true splitters=true controls=true" in result.stdout
