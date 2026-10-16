"""Actual raster and mouse-event coverage for a moving waveform viewport."""
import os
from pathlib import Path
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_waveform_viewport_render_and_marker_mapping():
    candidates = [ROOT / 'native/build/waveform_viewport_test',
                  ROOT / 'native/build/Release/waveform_viewport_test.exe',
                  ROOT / 'native/build-windows/Release/waveform_viewport_test.exe']
    binary = next((p for p in candidates if p.exists()), None)
    if binary is None:
        pytest.skip('Build waveform_viewport_test first')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    result = subprocess.run([str(binary)], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'waveform-viewport-ok' in result.stdout
