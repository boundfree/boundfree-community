"""Independent amplitude ranges and native scrolling regression."""
import os
from pathlib import Path
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]


def test_auto_amplitude_and_channel_scroll():
    candidates = [ROOT / 'native/build/waveform_channels_test',
                  ROOT / 'native/build/Release/waveform_channels_test.exe',
                  ROOT / 'native/build-windows/Release/waveform_channels_test.exe']
    binary = next((p for p in candidates if p.exists()), None)
    if binary is None:
        pytest.skip('Build waveform_channels_test first')
    result = subprocess.run([str(binary)], cwd=ROOT,
                            env=dict(os.environ, QT_QPA_PLATFORM='offscreen'),
                            capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stdout + result.stderr
    assert 'waveform-channels-ok' in result.stdout
