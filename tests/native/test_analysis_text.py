"""Full analysis attributes without nested scrolling, including translated content."""
import os
from pathlib import Path
import subprocess
import pytest

def test_analysis_properties_fit_complete_translated_text():
    root = Path(__file__).resolve().parents[2]
    app = next((p for p in [root / "native/build/bf_app", root / "native/build-windows/Release/bf_app.exe"] if p.is_file()), None)
    if app is None:
        pytest.skip("Build bf_app first")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    result = subprocess.run([str(app), "--smoke-ui-analysis-text"], cwd=root, env=env, capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "cases=40 no_scroll=true complete=true combos=true" in result.stdout
