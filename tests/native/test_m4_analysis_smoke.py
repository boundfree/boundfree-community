"""M4 AnalysisService 与三个自绘控件的行为测试。

被测对象: bf_app --smoke-m4-analysis 与 bf_app --smoke-m4-render (由
native/src/app/main.cpp 提供, 两个新增 smoke 入口)。

- --smoke-m4-analysis: 在后台线程池上发起 PSD 请求，验证结果非空且频段功率与同一 PSD 逐频段一致；
  FFT 结果不带频段功率；连续请求验证 latest-request-wins；取消请求验证没有回调；
  同通道 Coherence 验证 `analysis.same_channel` 结构化错误码。
- --smoke-m4-render: 离屏渲染 SpectrumWidget / SpectrogramWidget / BandTrendWidget
  三个自绘控件, 统计非背景像素, 证明三个 paintEvent 路径在喂入真实数据后产出
  实际像素 (而非只画空数据占位文本)。

本文件只通过子进程调用 bf_app 既有 smoke 入口, 不读取产品源文本, 不修改产品源 /
CMake / 黄金文件, 不运行全量测试。
"""

import os
import re
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


def _candidate_apps() -> list[Path]:
    # 与 tests/native/test_bf_app.py 的布局保持一致。
    return [
        ROOT / "native" / "build-windows" / "Release" / "bf_app.exe",
        ROOT / "native" / "build-windows" / "Debug" / "bf_app.exe",
        ROOT / "native" / "build" / "Release" / "bf_app.exe",
        ROOT / "native" / "build" / "Debug" / "bf_app.exe",
        ROOT / "native" / "build" / "bf_app.exe",
        ROOT / "native" / "build" / "bf_app",
    ]


def _resolve_app() -> Path:
    existing = [path for path in _candidate_apps() if path.exists()]
    if not existing:
        listed = "\n".join(f"- {path}" for path in _candidate_apps())
        pytest.skip(f"bf_app 可执行程序未找到, 请先构建 native。\nChecked:\n{listed}")
    return existing[0]


APP = _resolve_app()


def _run(args, check: bool = False) -> subprocess.CompletedProcess:
    # check 默认 False: 失败路径测试需要读取 stdout/stderr 后再断言退出码。
    env = os.environ.copy()
    env.setdefault("PYTHONPATH", str(ROOT / "sidecar"))
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("PYTHONIOENCODING", "utf-8")
    env.setdefault("PYTHONUTF8", "1")
    return subprocess.run(
        args,
        cwd=ROOT,
        env=env,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=check,
    )


def test_smoke_m4_analysis_psd_generation_cancel_and_same_channel_contracts():
    """后台 PSD、代次隔离、取消和同通道结构化错误均满足契约。"""
    result = _run([str(APP), "--smoke-m4-analysis"])

    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == (
        "smoke-m4-analysis-ok psd=true band_powers=true fft_band_powers=true "
        "latest_request=true cancel=true same_channel=analysis.same_channel"
    )
    assert "smoke-m4-analysis-failed" not in result.stderr


def test_smoke_m4_render_three_widgets_produce_pixels_offscreen():
    """三个自绘控件离屏渲染产出非背景像素。

    契约: bf_app --smoke-m4-render 以 0 退出, stdout 单行
    smoke-m4-render-ok spectrum=<int> spectrogram=<int> trend=<int>,
    三个整数均 > 0 且 (对 spectrum/spectrogram) 超过只画框架+占位文本的量级。

    覆盖 SpectrumWidget / SpectrogramWidget / BandTrendWidget 三个 paintEvent:
    - SpectrumWidget: 喂入 5 频点 Spectrum, paintEvent 应绘制频段色块 + 谱曲线,
      像素远超仅框架 + "No analysis data" 文本量级。
    - SpectrogramWidget: 喂入 3x3 Spectrogram, paintEvent 应填充 jet 色阶单元格
      矩阵 + colorbar, 像素量级最大。
    - BandTrendWidget: 喂入 5 频段 x 3 时刻趋势, paintEvent 应绘制 5 条折线。

    若任一 paintEvent 的数据分支断裂 (例如 showBands_/spectrogram_.values/times_
    被清空后未走数据路径而走空数据占位), 对应像素数会跌到框架+文本量级 (~千级以下),
    阈值断言变红。
    """
    result = _run([str(APP), "--smoke-m4-render"])

    assert result.returncode == 0, (
        f"expected m4 render smoke to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    match = re.search(
        r"smoke-m4-render-ok spectrum=(\d+) spectrogram=(\d+) trend=(\d+)",
        result.stdout,
    )
    assert match is not None, (
        f"expected smoke-m4-render-ok line with three pixel counts, got:\n{result.stdout}"
    )
    spectrum_pixels = int(match.group(1))
    spectrogram_pixels = int(match.group(2))
    trend_pixels = int(match.group(3))

    # 三个控件都必须有像素输出 (退出码已检查, 但显式断言更稳)。
    assert spectrum_pixels > 0
    assert spectrogram_pixels > 0
    assert trend_pixels > 0

    # 阈值: 捕获"数据分支断裂、只画框架+占位文本"的回归。
    # - spectrum/spectrogram 数据路径填充大块区域 (频段色块 / jet 矩阵 + colorbar),
    #   远超框架 (~1480 像素周长) + 标题/轴文本量级。
    # - trend 是折线图, 像素较稀疏但仍有 5 条线。
    MIN_SHAPE_PIXELS = 5000
    MIN_TREND_PIXELS = 500
    assert spectrum_pixels >= MIN_SHAPE_PIXELS, (
        f"spectrum pixels {spectrum_pixels} < {MIN_SHAPE_PIXELS}: "
        "SpectrumWidget 可能只画了框架+占位文本, 数据分支未触发"
    )
    assert spectrogram_pixels >= MIN_SHAPE_PIXELS, (
        f"spectrogram pixels {spectrogram_pixels} < {MIN_SHAPE_PIXELS}: "
        "SpectrogramWidget 可能只画了框架+占位文本, jet 色阶矩阵未填充"
    )
    assert trend_pixels >= MIN_TREND_PIXELS, (
        f"trend pixels {trend_pixels} < {MIN_TREND_PIXELS}: "
        "BandTrendWidget 可能只画了框架+占位文本, 折线未绘制"
    )
