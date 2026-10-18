"""Exercise displayed samples and independently compare exports with SciPy."""
import csv
import hashlib
import os
from pathlib import Path
import subprocess

import numpy as np
import pytest
from scipy import signal

from boundfree_sidecar.readers.registry import reader_for_path

ROOT = Path(__file__).resolve().parents[2]


def run_smoke(*args, timeout=60):
    candidates = [ROOT / "native/build/bf_app", ROOT / "native/build-windows/Release/bf_app.exe"]
    app = next((path for path in candidates if path.exists()), None)
    if app is None:
        pytest.skip("Build bf_app first")
    result = subprocess.run([str(app), *map(str, args)], cwd=ROOT,
                            env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                            capture_output=True, text=True, timeout=timeout)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


@pytest.mark.parametrize("n_channels", [1, 3])
def test_exports_keep_raw_filtered_car_bipolar_samples_and_markers(tmp_path, n_channels):
    fs = 250.0
    t = np.arange(2000) / fs
    data = np.asarray([10 * ch + 15 * np.sin(2 * np.pi * (8 + ch) * t)
                       + 7 * np.sin(2 * np.pi * 50 * t) for ch in range(n_channels)], dtype=np.float32)
    names = ["A", "B", "C"][:n_channels]
    source = tmp_path / "source.csv"
    with source.open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(["time_s", *names])
        writer.writerows(zip(t, *data))
    before = hashlib.sha256(source.read_bytes()).digest()
    assert "outputs=12" in run_smoke("--smoke-export-data-modes", source, tmp_path / "exports", timeout=210)
    assert hashlib.sha256(source.read_bytes()).digest() == before
    notch = signal.tf2sos(*signal.iirnotch(50, 30, fs))
    filtered = signal.sosfiltfilt(notch, data.astype(float), axis=1)
    bp = signal.butter(4, [1, 35], btype="bandpass", fs=fs, output="sos")
    # Channel B explicitly disables bandpass while retaining notch.
    bandpass_channels = [ch for ch in range(n_channels) if ch != 1]
    filtered[bandpass_channels] = signal.sosfiltfilt(bp, filtered[bandpass_channels], axis=1)
    expected = {"raw": data, "filtered": filtered,
                "car": data.astype(float) - data.astype(float).mean(axis=0),
                "bipolar": data[:-1].astype(float) - data[1:].astype(float) if n_channels > 1 else data}
    for mode, values in expected.items():
        for suffix in ("csv", "h5", "edf"):
            reader = reader_for_path(tmp_path / "exports" / f"{mode}.{suffix}")
            meta = reader.metadata()
            assert meta.fs == pytest.approx(fs)
            assert meta.ch_names == (["A-B", "B-C"] if mode == "bipolar" and n_channels > 1 else names)
            assert meta.events == [{"time": 0.5, "label": "stim"}]
            np.testing.assert_allclose(reader.get_slice(0, 4).data, values[:, 500:1500],
                                       atol=0.1 if suffix == "edf" else 2e-5, rtol=1e-6)
