"""Empty workspace and functional spin arrows across themes, styles and scaling."""
import os
from pathlib import Path
import subprocess
import pytest

@pytest.mark.parametrize("scale", ["1", "1.5", "2"])
@pytest.mark.parametrize("style", ["Fusion", "Windows"])
def test_empty_workspace_and_spin_buttons(scale, style):
    root = Path(__file__).resolve().parents[2]
    app = next((p for p in [root / "native/build/bf_app", root / "native/build-windows/Release/bf_app.exe"] if p.is_file()), None)
    if app is None:
        pytest.skip("Build bf_app first")
    env = {**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_SCALE_FACTOR": scale, "BF_UI_STYLE": style}
    result = subprocess.run([str(app), "--smoke-ui-empty-controls"], cwd=root, env=env, capture_output=True, text=True, timeout=30)
    if result.returncode == 77:
        pytest.skip(f"Qt style {style} unavailable")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "arrows=true clicks=true" in result.stdout
