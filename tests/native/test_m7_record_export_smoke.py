"""M7 录制与片段导出的端到端 smoke CLI 契约测试。

被测对象: ``bf_app --smoke-m7-record <fixture> <out_dir>`` 与
``bf_app --smoke-m7-export <fixture> <out_dir>`` 两个 smoke 入口
(由 ``native/src/app/main.cpp`` 暴露, 计划见 docs/archive/plans/m7-record-export.md)。

- record: 文件回放 chunk 经 Native 有界输入队列、二进制长度前缀 IPC 与边车单写线程,
  同时生成 EDF 与 HDF5; 期间追加一个事件, 停止后边车重新打开产物校验样本数与事件数,
  并报告 samples / source_dropped / writer_dropped / events 计数。
- export: 异步导出 CSV/HDF5/EDF 三格式并写入标注; 导出期间一个 1ms 定时器在主线程
  持续 tick, 证明写盘不阻塞 GUI 事件循环; 输出 ``formats=3 timer_ticks=...``。

本文件只通过子进程调用 bf_app 既有 smoke 入口, 不读取产品源文本, 不修改产品源 /
CMake / 黄金文件, 不运行全量测试。复用 ``tests/fixtures/generated/simple_eeg.csv``
作为文件回放源, 输出目录由 pytest ``tmp_path`` 提供, 不污染工作区。

实现未到位时 (bf_app 报 ``Unknown option``), 对应测试 skip, 避免在 TDD 阶段误红。
实现到位后应直接 pytest 通过。

不假设不存在的固定文本: 录制产物文件名含时间戳, 故只按扩展名 glob 断言产物存在,
不锁定文件名; 只断言成功/失败标记子串、退出码与可观测计数字段。
"""

import os
import re
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]

# 与 tests/native/test_bf_app.py 的布局保持一致。
SOURCE_FIXTURE = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"


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

    QCommandLineParser 在 ``--smoke-m7-*`` 未注册时打印
    ``Unknown option 'smoke-m7-...'`` 到 stderr 并以 1 退出; 此时该 smoke 入口
    尚未实现, 应 skip 而非 fail。实现到位后此函数返回 False, 进入契约断言。
    """
    combined = (result.stderr + result.stdout).lower()
    # QCommandLineParser 报 ``Unknown option 'smoke-m7-...'`` (不含 ``--`` 前缀),
    # 故用去前缀的 flag 名匹配, 避免漏判。
    flag_name = flag.lstrip("-").lower()
    return (
        result.returncode != 0
        and "unknown option" in combined
        and flag_name in combined
    )


def _parse_kv(line: str) -> dict[str, str]:
    """从 ``smoke-m7-*-ok key=val key=val`` 行解析 key=val 字段。"""
    fields: dict[str, str] = {}
    for token in line.split():
        if "=" in token:
            key, _, value = token.partition("=")
            fields[key] = value
    return fields


def test_smoke_m7_record_roundtrip_and_zero_drops(tmp_path: Path):
    """录制端到端: EDF+HDF5 双格式生成、单事件写入、计数一致、零丢弃。

    契约 (对应 RecordController 与 M7 验收标准):
    - 文件回放经 Native 有界队列与二进制 IPC 写入边车, 同时产出 EDF 与 HDF5。
    - 录制期间追加一个事件, 由同一写线程串行写入 HDF5 events 数据集。
    - 停止后边车重新打开产物校验, samples>0 且 events==1 才报 ok。
    - source_dropped 与 writer_dropped 必须各自为 0 (队列未满), 两者独立报告,
      不得合并成单一计数。

    断言: 退出码 0; stdout 含 ``smoke-m7-record-ok``; stderr 不含 failed 标记;
    解析计数字段: samples>0, source_dropped==0, writer_dropped==0, events==1;
    输出目录下存在恰好一个 .edf 与一个 .h5 产物 (产物名含时间戳, 故按扩展名 glob,
    不锁定固定文件名)。

    source_path 缺失时 skip; bf_app 未识别 flag 时 skip。
    """
    if not SOURCE_FIXTURE.exists():
        pytest.skip(f"录制源 fixture 缺失: {SOURCE_FIXTURE}")

    out_dir = tmp_path / "m7_record"
    out_dir.mkdir()

    result = _run([str(APP), "--smoke-m7-record", str(SOURCE_FIXTURE), str(out_dir)])

    if _flag_is_unsupported(result, "--smoke-m7-record"):
        pytest.skip("--smoke-m7-record 尚未实现 (bf_app 报 Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m7-record to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m7-record-ok" in result.stdout, (
        f"expected smoke-m7-record-ok marker in stdout, got:\n{result.stdout}"
    )
    assert "smoke-m7-record-failed" not in result.stderr, (
        f"unexpected failure marker in stderr:\n{result.stderr}"
    )

    # 解析 ``smoke-m7-record-ok samples=.. source_dropped=.. writer_dropped=.. events=..``
    ok_line = next(
        (ln for ln in result.stdout.splitlines() if "smoke-m7-record-ok" in ln),
        "",
    )
    fields = _parse_kv(ok_line)
    assert "samples" in fields, f"stdout 缺少 samples 字段: {result.stdout!r}"
    assert "source_dropped" in fields, f"stdout 缺少 source_dropped 字段: {result.stdout!r}"
    assert "writer_dropped" in fields, f"stdout 缺少 writer_dropped 字段: {result.stdout!r}"
    assert "events" in fields, f"stdout 缺少 events 字段: {result.stdout!r}"

    samples = int(fields["samples"])
    source_dropped = int(fields["source_dropped"])
    writer_dropped = int(fields["writer_dropped"])
    events = int(fields["events"])

    assert samples > 0, f"录制样本数必须为正, 实际 samples={samples}"
    assert source_dropped == 0, f"源队列不应丢弃 chunk, 实际 source_dropped={source_dropped}"
    assert writer_dropped == 0, f"写队列不应丢弃 chunk, 实际 writer_dropped={writer_dropped}"
    assert events == 1, f"应写入恰好一个事件, 实际 events={events}"

    # 产物文件名含时间戳 (smoke_YYYYMMDD_HHMMSS_mmmmmm.edf/.h5), 不锁定固定名,
    # 只按扩展名 glob 断言每种格式恰好一个产物。
    edf_files = list(out_dir.glob("*.edf"))
    h5_files = list(out_dir.glob("*.h5"))
    assert len(edf_files) == 1, (
        f"应生成恰好一个 EDF 产物, 实际 {len(edf_files)} 个: {edf_files}"
    )
    assert len(h5_files) == 1, (
        f"应生成恰好一个 HDF5 产物, 实际 {len(h5_files)} 个: {h5_files}"
    )
    assert edf_files[0].stat().st_size > 0, f"EDF 产物为空: {edf_files[0]}"
    assert h5_files[0].stat().st_size > 0, f"HDF5 产物为空: {h5_files[0]}"


def test_smoke_m7_export_three_formats_and_unblocked_timer(tmp_path: Path):
    """导出端到端: 三格式异步导出、标注写入、主线程定时器持续 tick。

    契约 (对应 ExportController 与 M7 验收标准):
    - 异步导出 CSV/HDF5/EDF 三格式, 每种格式完成后边车重新打开校验样本数与标注数。
    - 导出期间主线程 1ms 定时器持续 tick, 证明写盘在工作线程、不阻塞 GUI 事件循环;
      timer_ticks>0 才以 0 退出, 否则视为阻塞并失败。
    - 导出标注时间相对片段起点, 输出 ``formats=3 timer_ticks=...``。

    断言: 退出码 0; stdout 含 ``smoke-m7-export-ok`` 且 ``formats=3``、
    ``timer_ticks>0``; stderr 不含 failed 标记; 输出目录存在 m7_export.{csv,h5,edf}
    三个固定名产物 (导出产物名为固定 stem, 非时间戳)。

    source_path 缺失时 skip; bf_app 未识别 flag 时 skip。
    """
    if not SOURCE_FIXTURE.exists():
        pytest.skip(f"导出源 fixture 缺失: {SOURCE_FIXTURE}")

    out_dir = tmp_path / "m7_export"
    out_dir.mkdir()

    result = _run([str(APP), "--smoke-m7-export", str(SOURCE_FIXTURE), str(out_dir)])

    if _flag_is_unsupported(result, "--smoke-m7-export"):
        pytest.skip("--smoke-m7-export 尚未实现 (bf_app 报 Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m7-export to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m7-export-ok" in result.stdout, (
        f"expected smoke-m7-export-ok marker in stdout, got:\n{result.stdout}"
    )
    assert "smoke-m7-export-failed" not in result.stderr, (
        f"unexpected failure marker in stderr:\n{result.stderr}"
    )

    # 解析 ``smoke-m7-export-ok formats=3 timer_ticks=..``
    ok_line = next(
        (ln for ln in result.stdout.splitlines() if "smoke-m7-export-ok" in ln),
        "",
    )
    fields = _parse_kv(ok_line)
    assert "formats" in fields, f"stdout 缺少 formats 字段: {result.stdout!r}"
    assert "timer_ticks" in fields, f"stdout 缺少 timer_ticks 字段: {result.stdout!r}"

    formats = int(fields["formats"])
    timer_ticks = int(fields["timer_ticks"])

    assert formats == 3, f"应导出三种格式, 实际 formats={formats}"
    assert timer_ticks > 0, (
        f"导出期间主线程定时器必须持续 tick (证明非阻塞), 实际 timer_ticks={timer_ticks}"
    )

    # 导出产物使用固定 stem (m7_export.csv/.h5/.edf), 非时间戳文件名。
    csv_out = out_dir / "m7_export.csv"
    h5_out = out_dir / "m7_export.h5"
    edf_out = out_dir / "m7_export.edf"
    assert csv_out.is_file(), f"缺少 CSV 导出产物: {csv_out}"
    assert h5_out.is_file(), f"缺少 HDF5 导出产物: {h5_out}"
    assert edf_out.is_file(), f"缺少 EDF 导出产物: {edf_out}"
    assert csv_out.stat().st_size > 0, f"CSV 产物为空: {csv_out}"
    assert h5_out.stat().st_size > 0, f"HDF5 产物为空: {h5_out}"
    assert edf_out.stat().st_size > 0, f"EDF 产物为空: {edf_out}"
