"""M5 回放状态机与标注载入的 smoke CLI 契约测试。

被测对象: ``bf_app --smoke-m5-playback`` 与 ``bf_app --smoke-m5-markers <path>``
两个计划新增的 smoke 入口 (M5 计划 docs/plans/m5-playback-annotations.md)。
底层被测代码为 ``native/src/controller/playback_state.*`` 与
``native/src/controller/marker_store.*``, 由 ``native/src/app/main.cpp`` 暴露为
smoke 子命令。

本文件只通过子进程调用 bf_app 既有 smoke 入口, 不读取产品源文本, 不修改产品源
/ CMake / 黄金文件, 不运行全量测试。防御的可观测契约 (对应 M5 验收标准):

1. ``--smoke-m5-playback``: 验证 PlaybackState 状态机覆盖暂停/seek/五档倍速/
   尾部归零/位置钳制。以 0 退出, stdout 含 ``smoke-m5-playback-ok`` 成功标记,
   stderr 不含 ``smoke-m5-playback-failed`` 失败标记。

2. ``--smoke-m5-markers <path>``: 验证 MarkerStore 事件载入、排序、颜色轮换、
   标签编辑、删除。以 0 退出, stdout 含 ``smoke-m5-markers-ok`` 成功标记,
   stderr 不含 ``smoke-m5-markers-failed`` 失败标记。

实现未到位时 (bf_app 报 ``Unknown option``), 对应测试 skip, 避免在 TDD 阶段
误红。实现到位后应直接 pytest 通过。

不假设不存在的固定文本: 只断言成功/失败标记子串与退出码, 不锁定任意字段
名或数值格式 (实现者可自由选择 ``speeds=...`` / ``tail_zero=true`` 等措辞)。
"""

import os
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


def _flag_is_unsupported(result: subprocess.CompletedProcess, flag: str) -> bool:
    """bf_app 未识别 smoke flag 时以非 0 退出且报 Unknown option。

    QCommandLineParser 在 ``--smoke-m5-*`` 未注册时打印
    ``Unknown option 'smoke-m5-...'`` 到 stderr 并以 1 退出; 此时该 smoke 入口
    尚未实现, 应 skip 而非 fail。实现到位后此函数返回 False, 进入契约断言。
    """
    combined = (result.stderr + result.stdout).lower()
    # QCommandLineParser 报 ``Unknown option 'smoke-m5-...'`` (不含 ``--`` 前缀),
    # 故用去前缀的 flag 名匹配, 避免漏判。
    flag_name = flag.lstrip("-").lower()
    return (
        result.returncode != 0
        and "unknown option" in combined
        and flag_name in combined
    )


def _write_markers_data_csv(path: Path) -> None:
    # 与 tests/fixtures/generated/simple_eeg.csv 同构, 复用临时目录, 不依赖
    # 新增 fixture。markers smoke 入口可能需要打开一个真实 CSV 才能装载
    # MarkerStore; 临时 CSV 提供最小可打开会话。
    import csv

    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["time_s", "ch1", "ch2"])
        for i in range(10):
            writer.writerow([f"{i * 0.01:.2f}", float(i), float(i) + 10.0])


def test_smoke_m5_playback_state_machine_contracts():
    """PlaybackState 状态机覆盖暂停/seek/五档倍速/尾部归零/位置钳制。

    契约 (对应 playback_state.cpp/.h 与 M5 验收标准):
    - play/pause 切换 isPlaying; duration<=0 时 play 不启动。
    - seek 钳制到 [0, duration]。
    - setSpeed 仅接受 {0.25, 0.5, 1.0, 2.0, 4.0}; 非法值忽略。
    - advance 按 speed 推进, 触及尾部时 position 归零且 playing=false。
    任意状态机不变量断裂, smoke 以非 0 退出并打印 failed 标记。

    不锁定具体字段文本, 只断言: 退出码 0 + stdout 含 ok 标记 + stderr 不含
    failed 标记。这足以证明 smoke 真的跑了状态机路径而非橡皮图章。
    """
    result = _run([str(APP), "--smoke-m5-playback"])

    if _flag_is_unsupported(result, "--smoke-m5-playback"):
        pytest.skip("--smoke-m5-playback 尚未实现 (bf_app 报 Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m5-playback to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m5-playback-ok" in result.stdout, (
        f"expected smoke-m5-playback-ok marker in stdout, got:\n{result.stdout}"
    )
    assert "smoke-m5-playback-failed" not in result.stderr, (
        f"unexpected failure marker in stderr:\n{result.stderr}"
    )


def test_smoke_m5_markers_store_contracts(tmp_path: Path):
    """MarkerStore 事件载入/排序/颜色轮换/标签编辑/删除。

    契约 (对应 marker_store.cpp/.h 与 M5 验收标准):
    - replaceFromEvents 从 (time, label) 列表构造 markers。
    - add 按 time 升序插入 (std::upper_bound)。
    - nextColor 按 markers_.size() 轮换 8 色调色板。
    - updateLabel(index) / remove(index) 越界返回 false, 不抛。
    - clear 清空。
    任意不变量断裂, smoke 以非 0 退出并打印 failed 标记。

    使用临时 CSV (复用 simple_eeg 结构) 作为 markers smoke 的会话载体, 不依赖
    新增 fixture。markers 入口接受 <fixture-or-temp-path> 参数, 故传入临时 CSV。
    不锁定具体字段文本, 只断言 ok 标记 + 无 failed 标记 + 退出码 0。
    """
    data_csv = tmp_path / "m5_markers_session.csv"
    _write_markers_data_csv(data_csv)

    result = _run([str(APP), "--smoke-m5-markers", str(data_csv)])

    if _flag_is_unsupported(result, "--smoke-m5-markers"):
        pytest.skip("--smoke-m5-markers 尚未实现 (bf_app 报 Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m5-markers to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m5-markers-ok" in result.stdout, (
        f"expected smoke-m5-markers-ok marker in stdout, got:\n{result.stdout}"
    )
    assert "smoke-m5-markers-failed" not in result.stderr, (
        f"unexpected failure marker in stderr:\n{result.stderr}"
    )
