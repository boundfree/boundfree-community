"""Recording metadata form validation and MainWindow cancel wiring."""
import os
from pathlib import Path
import subprocess
import pytest

@pytest.mark.parametrize("name,args,marker", [
    ("record_metadata_dialog_test", [], "record-metadata-dialog-test-ok"),
    ("bf_app", ["--smoke-ui-record-form"], "smoke-ui-record-form-ok"),
])
def test_record_form(name, args, marker):
    root = Path(__file__).resolve().parents[2]
    candidates = [root / "native/build" / name,
                  root / "native/build-windows/Release" / (name + ".exe")]
    binary = next((p for p in candidates if p.exists()), None)
    if binary is None:
        pytest.skip("Build native form targets first")
    result = subprocess.run([str(binary), *args], cwd=root,
                            env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                            capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    assert marker in result.stdout
