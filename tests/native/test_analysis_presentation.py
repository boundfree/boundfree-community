"""Analysis diagrams and four-page result lifecycle, localized without recalculation."""
import os
from pathlib import Path
import subprocess
import pytest

@pytest.mark.parametrize("name,args,marker", [
    ("analysis_widgets_render_test", [], "Analysis exterior metadata, axes, locale, and malformed-model checks passed"),
    ("bf_app", ["--smoke-ui-analysis-presentation"], "smoke-ui-analysis-presentation-ok"),
])
def test_analysis_presentation(name, args, marker):
    root = Path(__file__).resolve().parents[2]
    candidates = [root / "native/build" / name,
                  root / "native/build-windows/Release" / (name + ".exe")]
    binary = next((p for p in candidates if p.exists()), None)
    if binary is None:
        pytest.skip("Build analysis presentation targets first")
    result = subprocess.run([str(binary), *args], cwd=root,
                            env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                            capture_output=True, text=True, timeout=35)
    assert result.returncode == 0, result.stdout + result.stderr
    assert marker in result.stdout
