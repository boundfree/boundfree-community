#!/usr/bin/env python3
"""Generate M3 DSP golden files from scipy (old processing/ parity source).

Product version stays 0.1.0-dev; this script does not touch version.json.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from scipy.signal import butter, iirnotch, sosfilt, sosfilt_zi, sosfiltfilt, tf2sos


def apply_reference(data: np.ndarray, mode: str) -> np.ndarray:
    if mode == "raw":
        return data.copy()
    if mode == "car":
        return data - data.mean(axis=0, keepdims=True)
    if mode == "bipolar":
        if data.shape[0] < 2:
            return data.copy()
        return data[:-1] - data[1:]
    raise ValueError(mode)


def design_bandpass(low: float, high: float, fs: float, order: int = 4) -> np.ndarray:
    nyq = fs / 2.0
    lo = max(low / nyq, 1e-4)
    hi = min(high / nyq, 0.9999)
    return butter(order, [lo, hi], btype="band", output="sos")


def design_notch(freq: float, fs: float, q: float = 30.0) -> np.ndarray:
    b, a = iirnotch(freq / (fs / 2.0), q)
    return tf2sos(b, a)


def offline_filter(data: np.ndarray, sos_list: list[np.ndarray]) -> np.ndarray:
    out = data.copy()
    for sos in sos_list:
        out = sosfiltfilt(sos, out, axis=1)
    return out


class FilterBank:
    def __init__(self, n_channels: int) -> None:
        self.n_channels = n_channels
        self.filters: dict[str, np.ndarray] = {}
        self.states: dict[str, np.ndarray] = {}
        self.enabled: dict[str, bool] = {}

    def add(self, name: str, sos: np.ndarray, enabled: bool = True) -> None:
        self.filters[name] = sos
        zi = sosfilt_zi(sos)
        self.states[name] = np.stack([zi] * self.n_channels, axis=1)
        self.enabled[name] = enabled

    def process(self, chunk: np.ndarray) -> np.ndarray:
        out = chunk.copy()
        for name, sos in self.filters.items():
            if not self.enabled.get(name, False):
                continue
            out, self.states[name] = sosfilt(sos, out, axis=1, zi=self.states[name])
        return out


class Pipeline:
    def rebuild(self, cfg: dict) -> None:
        self.cfg = cfg
        self.notch = [design_notch(float(f), float(cfg["fs"])) for f in cfg["notch_freqs"]]
        n_eff = int(cfg["n_channels"])
        if cfg["reference_mode"] == "bipolar" and n_eff >= 2:
            n_eff -= 1
        self.fb = FilterBank(n_eff)
        for i, sos in enumerate(self.notch):
            self.fb.add(f"notch{i}", sos)
        self.bp: list[list] = []
        for ch, setting in enumerate(cfg["bandpass"]):
            if not setting["enabled"] or setting["low"] >= setting["high"]:
                continue
            sos = design_bandpass(setting["low"], setting["high"], cfg["fs"])
            zi = sosfilt_zi(sos)
            self.bp.append([ch, sos, zi])

    def process_realtime(self, chunk: np.ndarray) -> np.ndarray:
        out = apply_reference(chunk.copy(), self.cfg["reference_mode"])
        out = self.fb.process(out)
        for state in self.bp:
            ch, sos, zi = state
            if ch >= out.shape[0]:
                continue
            y, new_zi = sosfilt(sos, out[ch], zi=zi)
            out[ch] = y
            state[2] = new_zi
        return out

    def process_offline(self, data: np.ndarray) -> np.ndarray:
        out = apply_reference(data.copy(), self.cfg["reference_mode"])
        if self.notch:
            out = offline_filter(out, self.notch)
        for ch, setting in enumerate(self.cfg["bandpass"]):
            if ch >= out.shape[0]:
                break
            if not setting["enabled"] or setting["low"] >= setting["high"]:
                continue
            sos = design_bandpass(setting["low"], setting["high"], self.cfg["fs"])
            out[ch] = offline_filter(out[ch : ch + 1], [sos])[0]
        return out


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    out_dir = root / "tests" / "golden" / "m3"
    out_dir.mkdir(parents=True, exist_ok=True)

    fs = 1000.0
    rng = np.random.default_rng(20260721)
    x = rng.standard_normal((4, 2048)).astype(np.float64)
    t = np.arange(2048) / fs
    for ch in range(4):
        x[ch] += (
            0.5 * np.sin(2 * np.pi * 50 * t + ch * 0.1)
            + 0.25 * np.sin(2 * np.pi * 100 * t)
            + 0.15 * np.sin(2 * np.pi * 150 * t)
        )

    cfg = {
        "fs": fs,
        "n_channels": 4,
        "reference_mode": "car",
        "notch_freqs": [50.0, 100.0, 150.0],
        "bandpass": [
            {"enabled": True, "low": 0.5, "high": 40.0},
            {"enabled": False, "low": 1.0, "high": 30.0},
            {"enabled": True, "low": 1.0, "high": 70.0},
            {"enabled": True, "low": 5.0, "high": 20.0},
        ],
    }

    np.save(out_dir / "input.npy", x)
    np.save(out_dir / "sos_bandpass_0_5_40.npy", design_bandpass(0.5, 40.0, fs))
    np.save(out_dir / "sos_notch_50.npy", design_notch(50.0, fs))
    np.save(out_dir / "out_ref_raw.npy", apply_reference(x, "raw"))
    np.save(out_dir / "out_ref_car.npy", apply_reference(x, "car"))
    np.save(out_dir / "out_ref_bipolar.npy", apply_reference(x, "bipolar"))

    offline_pipe = Pipeline()
    offline_pipe.rebuild(cfg)
    offline = offline_pipe.process_offline(x)
    np.save(out_dir / "out_e2e_offline.npy", offline)

    chunk_pipe = Pipeline()
    chunk_pipe.rebuild(cfg)
    chunks = [chunk_pipe.process_realtime(x[:, i : i + 64]) for i in range(0, x.shape[1], 64)]
    realtime_chunked = np.concatenate(chunks, axis=1)
    np.save(out_dir / "out_e2e_realtime_chunked.npy", realtime_chunked)

    full_pipe = Pipeline()
    full_pipe.rebuild(cfg)
    realtime_full = full_pipe.process_realtime(x)
    np.save(out_dir / "out_e2e_realtime_full.npy", realtime_full)

    meta = {
        "fs": fs,
        "seed": 20260721,
        "shape": list(x.shape),
        "config": cfg,
        "tolerances": {
            "coeff_rtol": 1e-9,
            "coeff_atol": 1e-12,
            "offline_rtol": 1e-6,
            "offline_atol": 1e-8,
            "realtime_rtol": 1e-5,
            "realtime_atol": 1e-7,
        },
        "generator": "scripts/common/generate_m3_golden.py",
        "product_version_note": "Product version stays 0.1.0-dev; do not bump for M3.",
        "max_abs_rt_full_vs_chunk": float(np.max(np.abs(realtime_full - realtime_chunked))),
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
