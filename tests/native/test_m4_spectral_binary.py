"""M4 频谱分析数值对齐测试。

被测对象: native/build/m4_spectral_test 可执行程序 (由 native/CMakeLists.txt 构建,
源码 native/src/dsp/m4_spectral_test.cpp)。该二进制加载 tests/golden/m4/ 下由
SciPy 参考实现生成的 .npy 黄金文件, 调用 native/src/dsp/spectral.* 的公共 API
(FFT 幅度 / Welch PSD / STFT / MSC 相干 / 频段功率), 逐项比对并报告 PASS/FAIL。

本文件只通过子进程调用既有可观测入口, 不读取产品源文本, 不修改产品源 / CMake /
黄金文件。重点断言三类可观测契约:

1. 真实黄金数据下, 二进制报告 m4-spectral-test-ok failures=0 并以 0 退出。
   若 dsp 算法对黄金数据的数值回归 (例如 computeCoherence 公式改变), 此测试变红。
2. 人为破坏的黄金数值下, 二进制报告 FAIL ... max_abs_err= 并以 1 退出 —— 证明该
   二进制不是橡皮图章, 容差检查确有约束力。
3. 黄金文件缺失时, 二进制以 2 退出并打印 EXCEPTION npy: cannot open ... ——
   证明错误路径契约稳定。
"""

import shutil
import subprocess
from pathlib import Path

import numpy as np
import pytest


ROOT = Path(__file__).resolve().parents[2]


def _candidate_binaries() -> list[Path]:
    # 与 tests/native/test_bf_app.py 的 _candidate_apps 保持一致的多平台布局。
    return [
        ROOT / "native" / "build" / "m4_spectral_test",
        ROOT / "native" / "build" / "Release" / "m4_spectral_test.exe",
        ROOT / "native" / "build" / "Debug" / "m4_spectral_test.exe",
        ROOT / "native" / "build-windows" / "Release" / "m4_spectral_test.exe",
        ROOT / "native" / "build-windows" / "Debug" / "m_spectral_test.exe",
    ]


def _resolve_binary() -> Path:
    existing = [path for path in _candidate_binaries() if path.exists()]
    if not existing:
        listed = "\n".join(f"- {path}" for path in _candidate_binaries())
        pytest.skip(f"m4_spectral_test 可执行程序未找到, 请先构建 native。\nChecked:\n{listed}")
    return existing[0]


BINARY = _resolve_binary()
GOLDEN_DIR = ROOT / "tests" / "golden" / "m4"


def _run(golden_dir: Path) -> subprocess.CompletedProcess:
    # cwd=ROOT 与 native/src/dsp/m4_spectral_test.cpp 默认参数一致。
    return subprocess.run(
        [str(BINARY), str(golden_dir)],
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def _combined(result: subprocess.CompletedProcess) -> str:
    # 该二进制把 PASS 明细打到 stdout, 把 FAIL 行与失败摘要打到 stderr, 需合并断言。
    return result.stdout + result.stderr


def test_m4_spectral_binary_reports_numerical_alignment_against_golden():
    """黄金数据集下二进制报告零失败。

    契约: 调用 m4_spectral_test tests/golden/m4, 退出码 0, stdout 结尾为
    m4-spectral-test-ok failures=0。此断言会在 dsp::computeWelchPsd /
    computeCoherence / computeSpectrogram 等任一函数相对 SciPy 黄金值的数值
    偏移超过 meta.json 记录容差时变红。
    """
    result = _run(GOLDEN_DIR)

    assert result.returncode == 0, (
        f"expected numerical alignment to pass, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "m4-spectral-test-ok failures=0" in result.stdout
    # 必须逐项比对而非空跑: 至少出现 FFT/Welch/STFT/相干/频段功率五类检查。
    assert "PASS fft_" in result.stdout
    assert "PASS welch_" in result.stdout
    assert "PASS stft_" in result.stdout
    assert "PASS coh" in result.stdout
    assert "PASS band_power" in result.stdout


# 每行: (golden 文件名, 破坏方式, 失败时 stderr/stdout 中应出现的失败标记片段)。
# 选取三类数值黄金 (相干 / Welch PSD / 频段功率) 覆盖 dsp::computeCoherence /
# computeWelchPsd / bandPower 三个独立代码路径, 确保容差检查确有约束力。
@pytest.mark.parametrize(
    "golden_name, perturb, fail_fragment",
    [
        # 相干数值整体偏移: 触发 computeCoherence 与黄金值偏离超过 coherence_atol。
        ("coh.npy", lambda a: a + 999.0, "FAIL coh "),
        # Welch PSD 数值整体偏移: 触发 computeWelchPsd 与黄金值偏离。
        ("welch_psd.npy", lambda a: a + 999.0, "FAIL welch_psd "),
        # 频段功率整体偏移: 触发 bandPower 与黄金值偏离超过 band_power_atol。
        ("band_power.npy", lambda a: a + 999.0, "FAIL band_power "),
    ],
)
def test_m4_spectral_binary_fails_when_golden_values_perturbed(
    tmp_path: Path, golden_name: str, perturb, fail_fragment: str
):
    """破坏黄金数值后二进制必须失败并指出超容差项。

    契约: 把指定黄金 .npy 整体偏移一个远超容差的常量后, 二进制以 1 退出, 输出
    FAIL <name> max_abs_err=<正数> 行, 并打印 m4-spectral-test-failed failures=N。
    若容差检查形同虚设 (例如永远返回 PASS), 此测试变红。
    """
    corrupt_dir = tmp_path / "m4_corrupt"
    corrupt_dir.mkdir()
    for src in GOLDEN_DIR.iterdir():
        shutil.copy(src, corrupt_dir / src.name)

    original = np.load(corrupt_dir / golden_name)
    perturbed = perturb(original)
    # 保留原 dtype 与形状, 仅改数值, 触发数值而非形状校验路径。
    np.save(corrupt_dir / golden_name, perturbed.astype(original.dtype))

    result = _run(corrupt_dir)
    combined = _combined(result)

    assert result.returncode == 1, (
        f"expected failure for perturbed {golden_name}, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "m4-spectral-test-failed failures=" in combined
    assert fail_fragment in combined, (
        f"expected {fail_fragment!r} in output, got:\n{combined}"
    )
    # 失败行必须携带非零 max_abs_err, 不能是空检查。
    fail_line = next(
        (line for line in combined.splitlines() if line.startswith(fail_fragment.strip())),
        None,
    )
    assert fail_line is not None
    assert "max_abs_err=" in fail_line
    assert "max_abs_err=0" not in fail_line  # 偏移后误差不可能为零


def test_m4_spectral_binary_reports_missing_golden_file(tmp_path: Path):
    """缺少任一黄金文件时二进制以 2 退出并打印异常。

    契约: 信号加载在 coherence 之前, 删除 signal.npy 后二进制无法启动, 以 2 退出,
    stderr 包含 EXCEPTION npy: cannot open ...signal.npy。证明错误路径稳定,
    且二进制确实依赖黄金文件而非硬编码回退。
    """
    missing_dir = tmp_path / "m4_missing"
    missing_dir.mkdir()
    for src in GOLDEN_DIR.iterdir():
        shutil.copy(src, missing_dir / src.name)
    (missing_dir / "signal.npy").unlink()

    result = _run(missing_dir)

    assert result.returncode == 2
    assert "EXCEPTION npy: cannot open" in result.stderr
    assert "signal.npy" in result.stderr
    # 失败路径不得误报成功。
    assert "m4-spectral-test-ok" not in result.stdout
