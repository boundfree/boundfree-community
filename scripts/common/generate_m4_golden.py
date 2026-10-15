#!/usr/bin/env python3
"""生成 M4 频谱分析黄金数据 (SciPy 参考实现)。

对齐目标:
- 旧仓库 boundfree_eeg/processing/spectrum.py (FFT 幅度 / Welch PSD / 频段功率)
- 旧仓库 boundfree_eeg/services/analysis_service.py (STFT / MSC 相干)

所有 SciPy 调用均显式给出关键参数,确保与 C++ native/src/dsp/spectral.cpp 的实现
在窗函数、去趋势、缩放、单边加倍、段重叠、频率上限等语义上完全对齐。

生成物 (tests/golden/m4/):
- signal.npy / signal_b.npy   : 确定性双通道合成信号 (float64)
- fft_amp.npy / fft_freq.npy  : FFT 幅度谱
- welch_psd.npy / welch_freq.npy         : Welch PSD 线性
- welch_psd_db.npy / welch_db_freq.npy   : Welch PSD dB
- stft_freq.npy / stft_time.npy / stft_sxx_db.npy  : STFT 密度 dB
- coh_freq.npy / coh.npy      : MSC 相干
- band_power.npy              : 五频段功率 (delta/theta/alpha/beta/gamma)
- meta.json                   : SciPy 参数、容差、形状说明

仅依赖 numpy/scipy,不依赖运行时产品代码。
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from scipy.signal import coherence as scipy_coherence
from scipy.signal import spectrogram as scipy_spectrogram
from scipy.signal import welch as scipy_welch


# 与 C++ eegBands() 一致的五频段定义 (Hz)。
BANDS = [
    ("delta", 0.5, 4.0),
    ("theta", 4.0, 8.0),
    ("alpha", 8.0, 13.0),
    ("beta", 13.0, 30.0),
    ("gamma", 30.0, 100.0),
]

# 与 C++ spectral.cpp 一致的 dB 下限。
DB_FLOOR = 1e-12


def make_signal(seed: int, fs: float, n: int) -> np.ndarray:
    """确定性合成信号:多正弦叠加 + 可复现高斯噪声。

    包含 10/alpha/beta/gamma 频率分量,覆盖五频段,且对窗函数边界敏感
    (整数周期 + 非整数周期混合),用于检验 FFT/Welch/STFT 的数值对齐。
    """
    rng = np.random.default_rng(seed)
    t = np.arange(n) / fs
    x = (
        1.0 * np.sin(2 * np.pi * 10.0 * t)        # alpha
        + 0.6 * np.sin(2 * np.pi * 20.0 * t)      # beta
        + 0.4 * np.sin(2 * np.pi * 40.0 * t)      # gamma
        + 0.3 * np.sin(2 * np.pi * 3.0 * t)       # delta/theta 边界
        + 0.2 * np.sin(2 * np.pi * 6.5 * t)       # 非整数周期 theta
    )
    x += 0.1 * rng.standard_normal(n)
    return x.astype(np.float64)


def make_signal_b(seed: int, fs: float, n: int) -> np.ndarray:
    """第二通道:与 signal 部分相关,用于 MSC 相干测试。

    b = 0.8 * a + 独立噪声 -> 相干应稳定在 0.6~1.0 之间,既有相关也有非平凡值。
    """
    rng = np.random.default_rng(seed)
    t = np.arange(n) / fs
    base = (
        0.8 * np.sin(2 * np.pi * 10.0 * t)
        + 0.4 * np.sin(2 * np.pi * 40.0 * t)
        + 0.2 * np.sin(2 * np.pi * 3.0 * t)
    )
    base += 0.15 * rng.standard_normal(n)
    return base.astype(np.float64)


def compute_fft_amp(signal: np.ndarray, fs: float) -> tuple[np.ndarray, np.ndarray]:
    """对齐 spectrum.py.compute_fft:np.hanning(n) 对称 Hann 窗 + np.fft.rfft。

    C++ computeFftAmplitude 使用 symmetricHann (0.5-0.5*cos(2π i/(n-1))) = np.hanning,
    abs*2/window.sum(),不去趋势。
    """
    n = len(signal)
    window = np.hanning(n)  # 对称 Hann,等价 C++ symmetricHann
    windowed = signal * window
    fft_vals = np.fft.rfft(windowed)
    freqs = np.fft.rfftfreq(n, d=1.0 / fs)
    amplitudes = np.abs(fft_vals) * 2.0 / window.sum()
    return freqs, amplitudes


def compute_welch(signal: np.ndarray, fs: float, nperseg: int, as_db: bool):
    """对齐 spectrum.py.compute_psd + scipy.signal.welch 默认语义。

    显式 window='hann' (周期 Hann = C++ periodicHann), detrend='constant' (按段去均值),
    scaling='density', noverlap 默认 nperseg//2 (C++ step=n/2)。
    """
    freqs, psd = scipy_welch(
        signal,
        fs=fs,
        window="hann",
        nperseg=nperseg,
        detrend="constant",
        scaling="density",
        return_onesided=True,
        average="mean",
    )
    if as_db:
        psd = 10.0 * np.log10(np.maximum(psd, DB_FLOOR))
    return freqs, psd


def compute_stft(signal: np.ndarray, fs: float, nperseg: int, noverlap: int,
                 t_start: float, fmax: float):
    """对齐 analysis_service._compute_stft。

    scipy.signal.spectrogram: window='hann' (周期), scaling='density', detrend='constant',
    boundary=None (无边界扩展), padded=False (丢弃尾部不完整段)。
    times + t_start; sxx_db = 10*log10(max(sxx, 1e-12))。
    C++ 时间轴: tStart + (start + n/2)/fs,与 scipy time = (nperseg/2 + k*step)/fs 一致。
    """
    fmax_bounded = min(float(fmax), fs / 2.0)
    freqs, times, sxx = scipy_spectrogram(
        signal,
        fs=fs,
        window="hann",
        nperseg=nperseg,
        noverlap=noverlap,
        detrend="constant",
        scaling="density",
        mode="psd",
    )
    f_mask = freqs <= fmax_bounded
    freqs = freqs[f_mask]
    sxx = sxx[f_mask, :]
    sxx_db = 10.0 * np.log10(np.maximum(sxx, DB_FLOOR))
    times = times + t_start
    return freqs, times, sxx_db


def compute_coherence(a: np.ndarray, b: np.ndarray, fs: float, nperseg: int, fmax: float):
    """对齐 analysis_service._compute_coherence。

    scipy.signal.coherence: window='hann', detrend='constant', 默认 noverlap=nperseg//2,
    Cxy = |Pxy|^2 / (Pxx*Pyy)。频率上限 mask: frequencies <= fmax。
    """
    fmax_bounded = min(float(fmax), fs / 2.0)
    freqs, cxy = scipy_coherence(a, b, fs=fs, nperseg=nperseg)
    mask = freqs <= fmax_bounded
    return freqs[mask], cxy[mask]


def band_power(psd: np.ndarray, freqs: np.ndarray, low: float, high: float) -> float:
    """对齐 spectrum.py.band_power:闭区间 [low,high] 频点求和 * df。"""
    mask = (freqs >= low) & (freqs <= high)
    if not mask.any():
        return 0.0
    df = freqs[1] - freqs[0]
    return float(np.sum(psd[mask]) * df)


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    out_dir = root / "tests" / "golden" / "m4"
    out_dir.mkdir(parents=True, exist_ok=True)

    fs = 250.0
    n = 2048
    seed = 20260721
    nperseg = 256
    noverlap = nperseg * 3 // 4  # 旧仓库 analysis_service 的重叠规则
    t_start = 12.5
    stft_fmax = 60.0
    coh_fmax = 80.0

    signal = make_signal(seed, fs, n)
    signal_b = make_signal_b(seed ^ 0xA5A5A5, fs, n)

    np.save(out_dir / "signal.npy", signal)
    np.save(out_dir / "signal_b.npy", signal_b)

    # FFT 幅度
    fft_freq, fft_amp = compute_fft_amp(signal, fs)
    np.save(out_dir / "fft_freq.npy", fft_freq)
    np.save(out_dir / "fft_amp.npy", fft_amp)

    # Welch PSD 线性
    welch_freq, welch_psd = compute_welch(signal, fs, nperseg, as_db=False)
    np.save(out_dir / "welch_freq.npy", welch_freq)
    np.save(out_dir / "welch_psd.npy", welch_psd)

    # Welch PSD dB
    _, welch_psd_db = compute_welch(signal, fs, nperseg, as_db=True)
    np.save(out_dir / "welch_psd_db.npy", welch_psd_db)
    np.save(out_dir / "welch_db_freq.npy", welch_freq)

    # STFT
    stft_freq, stft_time, stft_sxx_db = compute_stft(
        signal, fs, nperseg, noverlap, t_start, stft_fmax
    )
    np.save(out_dir / "stft_freq.npy", stft_freq)
    np.save(out_dir / "stft_time.npy", stft_time)
    np.save(out_dir / "stft_sxx_db.npy", stft_sxx_db)

    # MSC 相干
    coh_freq, coh = compute_coherence(signal, signal_b, fs, nperseg, coh_fmax)
    np.save(out_dir / "coh_freq.npy", coh_freq)
    np.save(out_dir / "coh.npy", coh)

    # 五频段功率 (基于 Welch 线性 PSD)
    band_powers = np.array(
        [band_power(welch_psd, welch_freq, lo, hi) for _, lo, hi in BANDS],
        dtype=np.float64,
    )
    np.save(out_dir / "band_power.npy", band_powers)

    meta = {
        "fs": fs,
        "n_samples": n,
        "seed": seed,
        "nperseg": nperseg,
        "noverlap_stft": noverlap,
        "t_start": t_start,
        "stft_fmax": stft_fmax,
        "coh_fmax": coh_fmax,
        "bands": [{"key": k, "low": lo, "high": hi} for k, lo, hi in BANDS],
        "scipy_params": {
            "welch": {
                "window": "hann",
                "nperseg": nperseg,
                "noverlap_default": nperseg // 2,
                "detrend": "constant",
                "scaling": "density",
                "return_onesided": True,
                "average": "mean",
            },
            "spectrogram": {
                "window": "hann",
                "nperseg": nperseg,
                "noverlap": noverlap,
                "detrend": "constant",
                "scaling": "density",
                "mode": "psd",
                "boundary": None,
                "padded": False,
            },
            "coherence": {
                "window": "hann",
                "nperseg": nperseg,
                "noverlap_default": nperseg // 2,
                "detrend": "constant",
            },
            "fft_amplitude": {
                "window": "np.hanning (symmetric Hann)",
                "normalization": "abs*2/window.sum()",
                "detrend": "none",
            },
            "db_floor": DB_FLOOR,
            "db_formula": "10*log10(max(value, 1e-12))",
        },
        "tolerances": {
            "fft_amp_atol": 1e-9,
            "welch_psd_atol": 1e-10,
            "welch_psd_db_atol": 1e-9,
            "stft_sxx_db_atol": 1e-9,
            "stft_freq_atol": 1e-12,
            "stft_time_atol": 1e-12,
            "coherence_atol": 1e-9,
            "band_power_atol": 1e-10,
            "freq_atol": 1e-12,
        },
        "shapes": {
            "signal": list(signal.shape),
            "signal_b": list(signal_b.shape),
            "fft_freq": list(fft_freq.shape),
            "fft_amp": list(fft_amp.shape),
            "welch_freq": list(welch_freq.shape),
            "welch_psd": list(welch_psd.shape),
            "welch_psd_db": list(welch_psd_db.shape),
            "stft_freq": list(stft_freq.shape),
            "stft_time": list(stft_time.shape),
            "stft_sxx_db": list(stft_sxx_db.shape),
            "coh_freq": list(coh_freq.shape),
            "coh": list(coh.shape),
            "band_power": list(band_powers.shape),
        },
        "alignment_notes": [
            "C++ symmetricHann = np.hanning = 0.5-0.5*cos(2π i/(n-1)); 用于 FFT 幅度。",
            "C++ periodicHann = scipy 'hann' window (DFT-even); 用于 Welch/STFT/Coherence。",
            "Welch 段重叠: C++ step=max(1,n//2) 等价 scipy noverlap=nperseg//2 默认值。",
            "STFT 段重叠: 旧仓库 noverlap=nperseg*3//4, C++ step=n-noverlap 一致。",
            "单边 PSD 加倍: 偶数 n 最后 Nyquist bin 不加倍; 奇数 n 全部加倍。",
            "相干 Cxy=|Pxy|^2/(Pxx*Pyy), 频率上限 mask freqs<=fmax (闭区间)。",
            "频段功率: 闭区间 [low,high] 频点求和 * df, 基于 Welch 线性 PSD。",
            "时间轴: scipy time=(nperseg/2 + k*step)/fs + t_start; C++ tStart+(start+n/2)/fs 一致。",
        ],
        "generator": "scripts/common/generate_m4_golden.py",
        "scipy_version": __import__("scipy").__version__,
        "numpy_version": np.__version__,
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")

    print(f"wrote {out_dir}")
    print(f"  fft_freq={fft_freq.shape} welch_freq={welch_freq.shape} "
          f"stft_freq={stft_freq.shape} stft_time={stft_time.shape} "
          f"coh_freq={coh_freq.shape} band_power={band_powers.shape}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
