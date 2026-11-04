"""Native RT-BCI decoding and virtual HTTP/TCP acquisition regressions."""
import os
import subprocess
import sys

import pytest

from test_bfsp_native import ROOT, native_binary


@pytest.mark.parametrize("name", ["rt_bci_decoder_test", "acquisition_wifi_source_test"])
def test_rt_bci_native(name):
    env = os.environ.copy()
    env.setdefault("BF_PYTHON_EXECUTABLE", sys.executable)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    result = subprocess.run([str(native_binary(name)), str(ROOT)], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, f"{name}\n{result.stdout}\n{result.stderr}"
    assert "PASS" in result.stdout + result.stderr, result.stdout + result.stderr


def test_rt_bci_paired_counter_waveform(tmp_path):
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    result = subprocess.run([str(native_binary("acquisition_waveform_test")), str(tmp_path / "paired.png")],
                            cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "ACQUISITION_WAVEFORM_TEST PASS" in result.stdout
