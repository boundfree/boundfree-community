"""Translated reference choices fit their selected field and popup in native styles."""
import os
from pathlib import Path
import subprocess
import pytest


@pytest.mark.parametrize("style", ["Fusion", "Windows"])
@pytest.mark.parametrize("scale", ["1", "1.5", "2"])
def test_reference_mode_labels(style, scale):
    root = Path(__file__).resolve().parents[2]
    app = next((p for p in [root / "native/build/bf_app", root / "native/build-windows/Release/bf_app.exe"] if p.is_file()), None)
    if app is None:
        pytest.skip("Build bf_app first")
    env = {**os.environ, "QT_QPA_PLATFORM": "offscreen", "BF_UI_STYLE": style, "QT_SCALE_FACTOR": scale}
    if env.get("BF_LAYOUT_SCREENSHOT_DIR"):
        env["BF_LAYOUT_SCREENSHOT_DIR"] = str(Path(env["BF_LAYOUT_SCREENSHOT_DIR"]) / scale)
    result = subprocess.run([str(app), "--smoke-ui-reference-labels"], cwd=root, env=env,
                            capture_output=True, text=True, timeout=30)
    if result.returncode == 77:
        pytest.skip(f"Qt style {style} unavailable")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "locales=5 themes=2 modes=3 popup=true wrapping=true" in result.stdout
