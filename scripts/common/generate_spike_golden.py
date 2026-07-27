#!/usr/bin/env python3
"""Generate deterministic Spike DSP golden input + structural ground truth.

This script is reproducible: a fixed numpy seed produces byte-identical output
on every run. It writes ONLY the input signal and the meta.json describing the
synthesis parameters, ground-truth injected peak times, and tolerances.

The self-consistent regression golden (detected timestamps, snippetLen, cluster
sizes, sort labels) is NOT produced here, because the C++ sortSpikes uses a
hand-written Jacobi eigensolver + a custom deterministic PRNG that cannot be
byte-aligned with sklearn/scipy. Instead, the C++ spike_golden_test produces
those regression artifacts on its first run (auto-generate-then-compare mode)
and subsequent runs compare against the frozen artifacts. The meta.json records
this scheme and the seed so the pipeline stays reproducible.

Sampling rate and all window lengths are derived from fs; nothing hardcodes
1 kHz or a fixed sample count inside the C++ side.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np


SEED = 20260724


def make_template_negative(pre: int, post: int, amplitude: float = 100.0) -> np.ndarray:
    """Class A: negative sharp peak (mirrors spike_dsp_test makeNegativeTemplate)."""
    length = pre + post
    t = np.zeros(length, dtype=np.float64)
    for i in range(length):
        rel = i - pre
        if rel <= 0:
            frac = i / pre if pre > 0 else 1.0
            t[i] = -amplitude * frac
        else:
            t[i] = -amplitude * np.exp(-rel / 8.0)
    return t


def make_template_biphasic(pre: int, post: int, amplitude: float = 70.0) -> np.ndarray:
    """Class B: biphasic (positive then negative), distinctly different shape."""
    length = pre + post
    t = np.zeros(length, dtype=np.float64)
    for i in range(length):
        rel = i - pre
        if rel <= 0:
            frac = i / pre if pre > 0 else 1.0
            t[i] = amplitude * frac
        else:
            t[i] = -amplitude * np.exp(-rel / 10.0)
    return t


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    out_dir = root / "tests" / "golden" / "spike"
    out_dir.mkdir(parents=True, exist_ok=True)

    rng = np.random.default_rng(SEED)

    fs = 30000.0
    duration_s = 2.0
    n = int(round(fs * duration_s))

    pre_ms = 0.5
    post_ms = 1.0
    pre_samples = int(round(pre_ms / 1000.0 * fs))
    post_samples = int(round(post_ms / 1000.0 * fs))
    snippet_len = pre_samples + post_samples

    tmpl_a = make_template_negative(pre_samples, post_samples, 100.0)
    tmpl_b = make_template_biphasic(pre_samples, post_samples, 70.0)

    # Baseline noise: std ~2 uV, spike amplitude 70-100 uV (far above noise).
    signal = rng.standard_normal(n).astype(np.float64) * 2.0

    # Inject spikes at known peak indices, alternating class A / class B.
    # Spacing > refractory (1 ms = 30 samples) and well clear of boundaries.
    peak_indices = [1000, 3000, 5500, 8000, 11000, 14000, 17000, 20000,
                    23000, 26000, 29000, 32000, 35000, 38000, 41000,
                    44000, 47000, 50000, 53000, 56000]
    # Alternate classes: even index -> A, odd index -> B
    template_labels = [(i % 2) for i in range(len(peak_indices))]

    injected = []
    for pk, label in zip(peak_indices, template_labels):
        tmpl = tmpl_a if label == 0 else tmpl_b
        start = pk - pre_samples
        end = pk + post_samples
        if start < 0 or end > n:
            continue
        signal[start:end] += tmpl
        injected.append({"peak_sample": int(pk), "template_label": int(label)})

    # Store as [1, N] float64 to align with npy_io.h (which loads 2D matrices;
    # 1D arrays come back as a single-row matrix).
    signal_2d = signal.reshape(1, n)
    np.save(out_dir / "input_signal.npy", signal_2d)

    detect_cfg = {
        "fs": fs,
        "threshold_mad": 6.0,
        "use_absolute_uv": False,
        "absolute_threshold_uv": 50.0,
        "detect_negative": True,
        "refractory_ms": 1.0,
        "pre_ms": pre_ms,
        "post_ms": post_ms,
    }
    sort_cfg = {
        "n_components": 3,
        "n_clusters": 2,
        "max_iters": 100,
        "seed": 42,
    }

    meta = {
        "fs": fs,
        "duration_s": duration_s,
        "n_samples": n,
        "seed": SEED,
        "signal_shape": [1, n],
        "snippet": {
            "pre_ms": pre_ms,
            "post_ms": post_ms,
            "pre_samples": pre_samples,
            "post_samples": post_samples,
            "snippet_len": snippet_len,
        },
        "detect_config": detect_cfg,
        "sort_config": sort_cfg,
        "ground_truth": {
            "n_injected": len(injected),
            "peaks": injected,
            "n_template_classes": 2,
        },
        "tolerances": {
            "detect_match_samples": 2,
            "detect_match_rate_min": 0.95,
            "sort_purity_min": 0.8,
            "timestamp_abs_tol_s": 2.0 / fs,
        },
        "golden_scheme": (
            "Self-consistent regression golden (detect_timestamps.npy, "
            "snippet_len, cluster_sizes.npy, sort_labels.npy) is produced by "
            "the C++ spike_golden_test on first run (auto-generate mode when "
            "the files are absent) and frozen; subsequent runs compare against "
            "the frozen artifacts. Structural correctness (detect count vs "
            "ground truth, sort purity vs ground-truth template labels) is "
            "asserted against meta.json ground_truth every run. sortSpikes "
            "uses hand-written Jacobi + custom PRNG (seed=42), so it is NOT "
            "byte-aligned with sklearn/scipy; only self-consistency is frozen."
        ),
        "generator": "scripts/generate_spike_golden.py",
        "product_version_note": "Product version stays 0.1.0-dev; do not bump.",
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out_dir} (input_signal.npy + meta.json)")
    print(f"  fs={fs} n={n} injected={len(injected)} snippet_len={snippet_len}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
