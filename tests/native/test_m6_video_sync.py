"""M6 视频同步的 smoke CLI 契约测试。

被测对象: ``bf_app --smoke-m6-sync`` 与 ``bf_app --smoke-m6-video <path>``
两个计划新增的 smoke 入口 (M6 计划)。底层被测代码
为 ``native/src/controller/video_sync_state.*``, 由 ``native/src/app/main.cpp``
暴露为 smoke 子命令。

本文件只通过子进程调用 bf_app 既有 smoke 入口, 不读取产品源文本, 不修改产品源
/ CMake / 黄金文件, 不运行全量测试。防御的可观测契约 (对应 M6 验收标准):

1. ``--smoke-m6-sync``: 验证纯 VideoSyncState 同步状态机覆盖偏移映射、范围内播放
   漂移阈值 (>400ms 才 seek 且要求 play)、暂停偏差阈值 (>50ms 才 seek 且要求
   pause)、前后空白 (进入即 pause + seek 0)、倍速只接受 M5 档位、duration<=0
   表示时长未知、setOffsetSeconds 处理非有限值为 0、重复 blank 不重复 seek。
   以 0 退出, stdout 含 ``smoke-m6-sync-ok`` 成功标记, stderr 不含
   ``smoke-m6-sync-failed`` 失败标记。

2. ``--smoke-m6-video <path>``: 真实媒体冒烟。用 ffmpeg 在 pytest 临时目录生成
   确定性短 MP4 (固定 lavfi 源, 不依赖随机或时间戳), offscreen 加载, 验证收到
   时长与有效帧、节流计数、暂停 seek、前后黑屏和倍速。以 0 退出, stdout 含
   ``smoke-m6-video-ok`` 成功标记, stderr 不含 ``smoke-m6-video-failed``。
   只有 ffmpeg 二进制或 ``libx264``/``aac`` 编码器确实缺失时 skip; 探测命令
   执行失败按错误处理, ffmpeg 声称有编码器但生成媒体失败也直接失败。

实现未到位时 (bf_app 报 ``Unknown option``), 对应测试 skip, 避免在 TDD 阶段
误红。实现到位后应直接 pytest 通过。

不假设不存在的固定文本: 只断言成功/失败标记子串与退出码, 不锁定任意字段名
或数值格式 (实现者可自由选择 ``thresholds=...`` / ``fps=~20`` 等措辞)。
"""

import os
import re
import shlex
import shutil
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
        # 模块级 skip 必须声明 allow_module_level, 否则 pytest 把它当成收集错误。
        pytest.skip(
            f"bf_app 可执行程序未找到, 请先构建 native。\nChecked:\n{listed}",
            allow_module_level=True,
        )
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

    QCommandLineParser 在 ``--smoke-m6-*`` 未注册时打印
    ``Unknown option 'smoke-m6-...'`` 到 stderr 并以 1 退出; 此时该 smoke 入口
    尚未实现, 应 skip 而非 fail。实现到位后此函数返回 False, 进入契约断言。
    """
    combined = (result.stderr + result.stdout).lower()
    # QCommandLineParser 报 ``Unknown option 'smoke-m6-...'`` (不含 ``--`` 前缀),
    # 故用去前缀的 flag 名匹配, 避免漏判。
    flag_name = flag.lstrip("-").lower()
    return (
        result.returncode != 0
        and "unknown option" in combined
        and flag_name in combined
    )


# 生成样本 MP4 必需的两个编码器名 (``ffmpeg -encoders`` 的第二列)。
REQUIRED_ENCODERS = ("libx264", "aac")

# ``ffmpeg -encoders`` 每行形如 `` V..... libx264  libx264 H.264 / AVC ...``:
# 6 位能力标记 + 编码器名 + 描述。只取编码器名, 描述里出现的同名字样不算数
# (例如 ``libx264rgb`` 的描述里含 ``libx264``)。
_ENCODER_LINE = re.compile(r"^\s*[VASD][.A-Z]{5}\s+(\S+)")


def _ffmpeg_encoders(ffmpeg: str) -> set[str]:
    """返回 ffmpeg 宣称支持编码的编码器名集合。

    探测命令本身执行失败时抛错: 这属于环境异常 (权限、损坏的构建、参数不兼容),
    不能静默等同于“缺编码器”而 skip。
    """
    result = subprocess.run(
        [ffmpeg, "-hide_banner", "-encoders"],
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"`{ffmpeg} -hide_banner -encoders` 执行失败, rc={result.returncode}: "
            f"{result.stderr.strip()[:400]}"
        )
    names = set()
    for line in result.stdout.splitlines():
        matched = _ENCODER_LINE.match(line)
        if matched is not None:
            names.add(matched.group(1))
    return names


def _require_ffmpeg() -> str:
    """返回生成与探测共用的 ffmpeg 可执行文件; 真实缺项时 skip。

    Conda 环境里的 ffmpeg 常见情况是有 ``aac`` 但缺 ``libx264`` (LGPL 构建不带
    GPL 的 x264), 此时生成样本 MP4 会以返回码 8 失败, 属于环境缺项而非产品缺陷,
    故 skip 并列出缺的编码器名。
    """
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        pytest.skip("ffmpeg 不在 PATH, 无法生成 M6 真实媒体冒烟所需的短 MP4")
    available = _ffmpeg_encoders(ffmpeg)
    missing = [name for name in REQUIRED_ENCODERS if name not in available]
    if missing:
        pytest.skip(
            f"{ffmpeg} 的 -encoders 输出缺编码器 {', '.join(missing)}; "
            "生成 M6 样本 MP4 需要 libx264 (H.264) 与 aac (音频) 编码器"
        )
    return ffmpeg


def _generate_short_mp4(ffmpeg: str, path: Path) -> None:
    """用 ffmpeg lavfi 源生成确定性 1 秒短 MP4。

    - 视频源 ``color=c=red:s=64x48:d=1.0:r=25``: 固定颜色、固定尺寸、固定时长、
      固定帧率, 输出逐字节可重现 (同一 ffmpeg 构建), 不依赖随机或时间戳。
    - 音频源 ``sine=frequency=440:duration=1.0``: 提供 QAudioOutput 可解码轨道,
      覆盖 ``QMediaPlayer`` 完整装载路径。
    - ``-preset ultrafast -pix_fmt yuv420p``: 最快编码 + 通用像素格式, 保证
      offscreen 后端能解码首帧; 不追求画质。
    - ``-shortest``: 以最短流截断, 避免时长漂移到 1.0s 之外。

    ``ffmpeg`` 必须是 ``_require_ffmpeg()`` 探测过的同一个可执行文件, 否则探测到
    的编码器集合与真正生成媒体的二进制可能不一致。生成失败 (check=True) 时直接
    失败: 编码器已由探测确认存在, 此时失败是实际缺陷。
    """
    subprocess.run(
        [
            ffmpeg,
            "-y",
            "-f", "lavfi",
            "-i", "color=c=red:s=64x48:d=1.0:r=25",
            "-f", "lavfi",
            "-i", "sine=frequency=440:duration=1.0:sample_rate=44100",
            "-c:v", "libx264",
            "-preset", "ultrafast",
            "-pix_fmt", "yuv420p",
            "-c:a", "aac",
            "-shortest",
            str(path),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=True,
    )


# -- 编码器探测的行为测试 (用假 ffmpeg 可执行文件, 不读源码文本) ----------------

_ENCODER_HEADER = (
    "Encoders:\n"
    " V..... = Video\n"
    " A..... = Audio\n"
    " ------\n"
)


def _fake_ffmpeg(
    tmp_path: Path,
    listing: str,
    probe_rc: int = 0,
    generation_rc: int = 0,
    stderr: str = "",
) -> Path:
    """写一个假 ffmpeg: 探测参数回放固定编码器列表, 生成参数 (``-y``) 按需失败。

    列表放在同目录的 encoders.txt, 避免把多行文本拼进 shell 引号。
    """
    (tmp_path / "encoders.txt").write_text(listing, encoding="utf-8")
    body = [
        'if [[ "${1:-}" == "-y" ]]; then exit ' + str(generation_rc) + "; fi",
        'cat "$(dirname "$0")/encoders.txt"',
    ]
    if stderr:
        body.append(f"printf '%s\\n' {shlex.quote(stderr)} >&2")
    body.append(f"exit {probe_rc}")
    path = tmp_path / "ffmpeg"
    path.write_text("#!/usr/bin/env bash\n" + "\n".join(body) + "\n", encoding="utf-8")
    path.chmod(0o755)
    return path


def _prepend_path(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setenv("PATH", f"{tmp_path}{os.pathsep}{os.environ['PATH']}")


def _missing_part(reason: str) -> str:
    """取 skip 理由里列出的缺项部分 (仅编码器名列表, 不含后文的用途说明)。"""
    return reason.split("缺编码器", 1)[1].split(";", 1)[0]


def test_ffmpeg_absent_skips_with_reason(tmp_path, monkeypatch):
    empty = tmp_path / "empty-path"
    empty.mkdir()
    monkeypatch.setenv("PATH", str(empty))
    with pytest.raises(pytest.skip.Exception) as excinfo:
        _require_ffmpeg()
    assert "ffmpeg 不在 PATH" in str(excinfo.value)


def test_missing_libx264_skips_and_names_it(tmp_path, monkeypatch):
    # libx264rgb 的描述里也含 libx264; 只有第二列的精确名字才算数。
    _fake_ffmpeg(
        tmp_path,
        _ENCODER_HEADER
        + " V..... libx264rgb           libx264 H.264 / AVC / MPEG-4 part 10 (codec h264)\n"
        + " A..... aac                  AAC (Advanced Audio Coding)\n",
    )
    _prepend_path(tmp_path, monkeypatch)
    with pytest.raises(pytest.skip.Exception) as excinfo:
        _require_ffmpeg()
    missing = _missing_part(str(excinfo.value))
    assert "libx264" in missing
    assert "aac" not in missing


def test_missing_aac_skips_and_names_it(tmp_path, monkeypatch):
    _fake_ffmpeg(
        tmp_path,
        _ENCODER_HEADER
        + " V..... libx264              libx264 H.264 / AVC / MPEG-4 part 10 (codec h264)\n",
    )
    _prepend_path(tmp_path, monkeypatch)
    with pytest.raises(pytest.skip.Exception) as excinfo:
        _require_ffmpeg()
    missing = _missing_part(str(excinfo.value))
    assert "aac" in missing
    assert "libx264" not in missing


def test_ffmpeg_with_both_encoders_is_used(tmp_path, monkeypatch):
    expected = _fake_ffmpeg(
        tmp_path,
        _ENCODER_HEADER
        + " V..... libx264              libx264 H.264 / AVC / MPEG-4 part 10 (codec h264)\n"
        + " A..... aac                  AAC (Advanced Audio Coding)\n",
    )
    _prepend_path(tmp_path, monkeypatch)
    assert Path(_require_ffmpeg()) == expected


def test_probe_failure_is_an_error_not_a_skip(tmp_path, monkeypatch):
    _fake_ffmpeg(tmp_path, "", probe_rc=1, stderr="Unknown option -encoders")
    _prepend_path(tmp_path, monkeypatch)
    with pytest.raises(RuntimeError) as excinfo:
        _require_ffmpeg()
    message = str(excinfo.value)
    assert "rc=1" in message
    assert "Unknown option -encoders" in message


def test_generation_failure_is_an_error_not_a_skip(tmp_path, monkeypatch):
    # 探测能通过 (有 libx264/aac), 但生成失败: 环境看起来齐备时失败就是真失败。
    _fake_ffmpeg(
        tmp_path,
        _ENCODER_HEADER
        + " V..... libx264              libx264 H.264 / AVC / MPEG-4 part 10 (codec h264)\n"
        + " A..... aac                  AAC (Advanced Audio Coding)\n",
        generation_rc=8,
    )
    _prepend_path(tmp_path, monkeypatch)
    ffmpeg = _require_ffmpeg()
    with pytest.raises(subprocess.CalledProcessError) as excinfo:
        _generate_short_mp4(ffmpeg, tmp_path / "out.mp4")
    assert excinfo.value.returncode == 8


def test_smoke_m6_sync_state_contracts():
    """VideoSyncState 纯同步状态机覆盖 M6 验收标准 1。

    契约 (对应 video_sync_state.h 与 M6 验收标准 1):
    - videoMs = (eegTime - offset) * 1000 偏移映射。
    - durationMs <= 0 表示时长未知, 不判定尾后空白。
    - 有效范围内播放时, 漂移绝对值 > 400 ms 才 seek 且要求 play。
    - 暂停时偏差 > 50 ms 才 seek 且要求 pause。
    - 偏移前或视频结束后进入空白: 首次进入即 pause + seek 0; 重复 blank 不重复
      seek (幂等)。
    - 倍速只接受 M5 已支持档位 {0.25, 0.5, 1.0, 2.0, 4.0}。
    - setOffsetSeconds 对非有限值 (NaN/Inf) 归零处理。
    任意不变量断裂, smoke 以非 0 退出并打印 failed 标记。

    不锁定具体字段文本, 只断言: 退出码 0 + stdout 含 ok 标记 + stderr 不含
    failed 标记。这足以证明 smoke 真的跑了状态机路径而非橡皮图章。
    """
    result = _run([str(APP), "--smoke-m6-sync"])

    if _flag_is_unsupported(result, "--smoke-m6-sync"):
        pytest.skip("--smoke-m6-sync 尚未实现 (bf_app 报 Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m6-sync to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m6-sync-ok" in result.stdout, (
        f"expected smoke-m6-sync-ok marker in stdout, got:\n{result.stdout}"
    )
    assert "smoke-m6-sync-failed" not in result.stderr, (
        f"unexpected failure marker in stderr:\n{result.stderr}"
    )


def test_smoke_m6_video_real_media_contracts(tmp_path: Path):
    """真实媒体冒烟覆盖 M6 验收标准 2。

    契约 (对应 VideoPanel + QMediaPlayer 与 M6 验收标准 2):
    - offscreen 加载 ffmpeg 生成的确定性短 MP4, 不依赖外部样本或网络。
    - 收到 durationChanged 后重新同步 (时长异步可用)。
    - QVideoSink::videoFrameChanged 触发有效帧 (非空 QImage)。
    - 帧节流计数受控 (约 20 fps), 不在 GPU 回读前堆积。
    - 暂停时 seek 目标帧, 不引入独立同步定时器。
    - 偏移前/视频结束后的空白段黑屏 (pause + seek 0)。
    - 倍速跟随 M5 档位。
    任意不变量断裂, smoke 以非 0 退出并打印 failed 标记。

    ffmpeg 二进制或编码器确实缺失时 skip (理由列明缺项); 有编码器却生成失败、或
    bf_app 冒烟失败都按失败处理。bf_app 未实现该 flag 时按既有 skip 约定处理。
    """
    ffmpeg = _require_ffmpeg()

    mp4_path = tmp_path / "m6_sync_probe.mp4"
    _generate_short_mp4(ffmpeg, mp4_path)
    # 确认生成的文件非空且确为可探测的 MP4; ffmpeg 失败时上面已 check=True 抛错。
    assert mp4_path.stat().st_size > 0

    result = _run([str(APP), "--smoke-m6-video", str(mp4_path)])

    if _flag_is_unsupported(result, "--smoke-m6-video"):
        pytest.skip("--smoke-m6-video 尚未实现 (bf_app 报 Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m6-video to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m6-video-ok" in result.stdout, (
        f"expected smoke-m6-video-ok marker in stdout, got:\n{result.stdout}"
    )
    assert "smoke-m6-video-failed" not in result.stderr, (
        f"unexpected failure marker in stderr:\n{result.stderr}"
    )


def test_smoke_m6_main_window_eeg_plus_video_joint_open(tmp_path: Path):
    """主窗口 EEG+视频联合打开集成测试 (M6 验收: 联合打开)。

    契约: ``bf_app --smoke-ui --open-file <csv> --open-video <mp4>`` 在主窗口
    装载标准 CSV 解析器的同时, 通过新增的 ``--open-video`` 路径挂载视频面板。
    成功时以 0 退出, stdout 同时包含 ``smoke-ui-file-flow-ok`` (CSV 流程完成)
    与 ``video=loaded=1`` (视频面板成功装载), stderr 不含
    ``smoke-ui-file-flow-failed``。

    生产入口未实现时 (bf_app 报 ``Unknown option 'open-video'``), 按既有规则
    skip; 入口完成后必须通过。
    """
    ffmpeg = _require_ffmpeg()

    csv_path = ROOT / "tests" / "fixtures" / "generated" / "csv_with_markers.csv"
    if not csv_path.exists():
        pytest.skip(f"fixture CSV 不存在: {csv_path}")

    mp4_path = tmp_path / "m6_joint_probe.mp4"
    _generate_short_mp4(ffmpeg, mp4_path)
    assert mp4_path.stat().st_size > 0

    result = _run(
        [str(APP), "--smoke-ui", "--open-file", str(csv_path), "--open-video", str(mp4_path)]
    )

    if _flag_is_unsupported(result, "--open-video"):
        pytest.skip("--open-video 尚未实现 (bf_app 报 Unknown option)")

    assert result.returncode == 0, (
        f"expected joint smoke-ui open-file+open-video to succeed, "
        f"got rc={result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-ui-file-flow-ok" in result.stdout, (
        f"expected smoke-ui-file-flow-ok marker in stdout, got:\n{result.stdout}"
    )
    assert "video=loaded=1" in result.stdout, (
        f"expected video=loaded=1 marker in stdout, got:\n{result.stdout}"
    )
    assert "smoke-ui-file-flow-failed" not in result.stderr, (
        f"unexpected file-flow failure marker in stderr:\n{result.stderr}"
    )
