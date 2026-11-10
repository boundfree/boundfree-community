"""``scripts/wsl/verify_m1_foundation.sh`` 的入口测试。

被测对象: 仓库的总验证脚本及其构建目录解析。契约:

1. 脚本读 ``BF_NATIVE_BUILD_DIR``, 未设置时默认 ``native/build-qt6-baseline``;
   本测试用同一规则判断产物是否存在, 不另立默认值。
2. 构建目录里没有 ``bf_app`` 时 skip 并写明缺的是什么、如何指到已构建目录:
   ``--fast`` 不构建, 脚本也就无法给出有意义的验证结果。
   产物存在时脚本失败必须让测试失败, 不能退化成 skip。
3. 设 ``BF_VERIFY_SKIP_LICENSE_GATE=1`` 时脚本给内层 pytest 加
   ``--deselect tests/native/test_m8_license_gate.py``; 不设时保持默认执行。
   本测试调用脚本时始终设置该开关: 授权门禁构建在外层 pytest 里已经单独跑过一次。
4. ``--fast`` 只跳过 CMake 构建与界面自检, 内层 ``pytest tests --ignore=tests/scripts``
   照常执行, 所以本测试仍然是一次嵌套回归, 不是轻量检查。

参数构造用假的 Python 解释器 (记录 argv 后以 0 退出) 验证, 不读脚本源码文本;
真实执行只在产物存在时做一次, 并保留脚本的失败语义。
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "wsl" / "verify_m1_foundation.sh"
BUILD_DIR_ENV = "BF_NATIVE_BUILD_DIR"
DEFAULT_BUILD_DIR = ROOT / "native" / "build-qt6-baseline"
LICENSE_GATE_TEST = "tests/native/test_m8_license_gate.py"


def _usable_bash() -> str | None:
    bash = shutil.which("bash")
    if bash is None:
        return None
    # Windows ships a WSL interop bash.exe under System32. It is not a usable
    # host shell for running repository scripts with Windows paths.
    if sys.platform == "win32":
        normalized = bash.replace("/", "\\").lower()
        if normalized.endswith("\\system32\\bash.exe") or normalized.endswith("\\sysnative\\bash.exe"):
            return None
    return bash


def _build_dir(env: dict[str, str] | None = None, cwd: Path | None = None) -> Path:
    """与脚本同一规则: 未设置或空字符串取默认目录, 相对路径按调用目录展开。

    不做 strip 也不展开 ``~``: 脚本拿原始字符串比较开头是不是 ``/``, 再拼上
    ``$PWD``, 所以全空格的值或字面 ``~`` 在那里就是目录名的一部分。这里不做
    额外规范化, 避免两边的产物查找规则悄悄分叉。

    ``cwd`` 默认取当前工作目录, 即调用方启动脚本时所在的目录。
    """
    source = os.environ if env is None else env
    value = source.get(BUILD_DIR_ENV)
    if not value:
        return DEFAULT_BUILD_DIR
    path = Path(value)
    return path if path.is_absolute() else Path(cwd or Path.cwd()) / path


def _require_built_app(build_dir: Path) -> Path:
    """返回构建目录里的 ``bf_app``; 只看产物是否存在, 不预判脚本能否成功。"""
    app = build_dir / "bf_app"
    if not app.is_file():
        pytest.skip(
            f"构建目录里没有 bf_app: {app}。--fast 不构建, 缺产物时无法验证脚本; "
            f"用 {BUILD_DIR_ENV} 指向已构建目录 (例如 native/build) 后重跑"
        )
    return app


def _script_env(
    build_dir: Path,
    python: str,
    extra: dict[str, str] | None = None,
    unset: tuple[str, ...] = (),
) -> dict[str, str]:
    env = {
        **os.environ,
        "PYTHONPATH": str(ROOT / "sidecar"),
        "QT_QPA_PLATFORM": "offscreen",
        BUILD_DIR_ENV: str(build_dir),
        "BF_PYTHON_EXECUTABLE": python,
        # 外层 pytest 已单独跑过授权门禁构建, 内层重复执行只会再花一次最耗内存的构建。
        "BF_VERIFY_SKIP_LICENSE_GATE": "1",
    }
    env.pop("BF_VERIFY_FAKE_CALL_LOG", None)
    for key in unset:
        env.pop(key, None)
    if extra:
        env.update(extra)
    return env


def _run_script(
    build_dir: Path,
    python: str,
    extra_env: dict[str, str] | None = None,
    unset_env: tuple[str, ...] = (),
) -> subprocess.CompletedProcess:
    """运行脚本的 ``--fast`` 路径; 脚本非 0 退出时抛 CalledProcessError, 不吞成 skip。"""
    bash = _usable_bash()
    if bash is None:
        pytest.skip("usable bash is unavailable on this platform")
    assert SCRIPT.exists(), SCRIPT
    return subprocess.run(
        [bash, str(SCRIPT), "--fast"],
        cwd=ROOT,
        env=_script_env(build_dir, python, extra_env, unset_env),
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )


def _write_fake_python(path: Path) -> Path:
    """写一个记录 argv 后以 0 退出的假解释器, 用于检查脚本拼出的 pytest 参数。"""
    path.write_text(
        "#!/usr/bin/env bash\n"
        "{\n"
        "  printf 'CALL\\n'\n"
        "  printf '%s\\n' \"$@\"\n"
        "} >> \"$BF_VERIFY_FAKE_CALL_LOG\"\n",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def _pytest_calls(log: Path) -> list[list[str]]:
    """从假解释器的调用日志里取出所有 ``-m pytest`` 调用及其参数。"""
    calls: list[list[str]] = []
    current: list[str] = []
    for line in log.read_text(encoding="utf-8").splitlines():
        if line == "CALL":
            if current:
                calls.append(current)
            current = []
        else:
            current.append(line)
    if current:
        calls.append(current)
    return [call for call in calls if len(call) >= 2 and call[0] == "-m" and call[1] == "pytest"]


def test_build_dir_falls_back_to_baseline_build_directory():
    assert _build_dir({}) == ROOT / "native" / "build-qt6-baseline"
    # 脚本用 ${VAR:-default}: 未设置与空字符串都取默认目录。
    assert _build_dir({BUILD_DIR_ENV: ""}) == DEFAULT_BUILD_DIR


def test_relative_build_dir_is_resolved_from_the_calling_directory(tmp_path):
    # 相对路径真的能找到调用目录下的产物 (而不是落到默认目录), 与脚本拼接 $PWD 一致。
    caller = tmp_path / "caller"
    build = caller / "out" / "build"
    build.mkdir(parents=True)
    (build / "bf_app").write_bytes(b"")

    assert _require_built_app(_build_dir({BUILD_DIR_ENV: "out/build"}, cwd=caller)) == build / "bf_app"


def test_build_dir_keeps_values_the_script_would_not_normalize(tmp_path):
    # 脚本不做 strip, 也不展开 ~: 全空格的值就是空格目录名, 字面 ~ 就是 ~ 目录。
    assert _build_dir({BUILD_DIR_ENV: " "}, cwd=tmp_path) == tmp_path / " "
    assert _build_dir({BUILD_DIR_ENV: "~/native/build"}, cwd=tmp_path) == tmp_path / "~" / "native" / "build"


def test_absolute_build_dir_is_used_as_given(tmp_path):
    absolute = tmp_path / "community-a10"
    assert _build_dir({BUILD_DIR_ENV: str(absolute)}) == absolute


def test_missing_bf_app_skips_with_explicit_reason(tmp_path):
    build_dir = tmp_path / "community-a10"
    build_dir.mkdir()
    with pytest.raises(pytest.skip.Exception) as excinfo:
        _require_built_app(build_dir)
    reason = str(excinfo.value)
    assert "bf_app" in reason
    assert str(build_dir / "bf_app") in reason
    assert BUILD_DIR_ENV in reason


def _fake_call_log_run(tmp_path: Path, extra_env=None, unset_env=()) -> list[list[str]]:
    """用假解释器跑一次脚本, 返回内层 pytest 收到的参数。"""
    build_dir = tmp_path / "build"
    build_dir.mkdir(exist_ok=True)
    (build_dir / "bf_app").write_bytes(b"")
    fake_python = _write_fake_python(tmp_path / "fake-python")
    log = tmp_path / "calls.log"
    extra = {"BF_VERIFY_FAKE_CALL_LOG": str(log)}
    if extra_env:
        extra.update(extra_env)

    _run_script(build_dir, str(fake_python), extra, unset_env)

    calls = _pytest_calls(log)
    assert len(calls) == 1, calls
    return calls[0]


def test_license_gate_is_deselected_when_switch_is_one(tmp_path):
    assert _fake_call_log_run(tmp_path, {"BF_VERIFY_SKIP_LICENSE_GATE": "1"}) == [
        "-m",
        "pytest",
        "tests",
        "-q",
        "--ignore=tests/scripts",
        "--deselect",
        LICENSE_GATE_TEST,
    ]


@pytest.mark.parametrize(
    "extra_env, unset_env",
    [
        ({"BF_VERIFY_SKIP_LICENSE_GATE": "0"}, ()),
        ({}, ("BF_VERIFY_SKIP_LICENSE_GATE",)),
    ],
    ids=["explicit-zero", "unset"],
)
def test_license_gate_runs_unless_switch_is_one(tmp_path, extra_env, unset_env):
    # 显式 0 与完全未设置都必须保持默认: 内层仍跑授权门禁, 且 --fast 不裁掉回归。
    args = _fake_call_log_run(tmp_path, extra_env, unset_env)
    assert args[:5] == ["-m", "pytest", "tests", "-q", "--ignore=tests/scripts"]
    assert LICENSE_GATE_TEST not in args


def test_script_failure_with_built_app_is_an_error_not_a_skip(tmp_path):
    build_dir = tmp_path / "build"
    build_dir.mkdir()
    (build_dir / "bf_app").write_bytes(b"")
    # 产物存在, 因此不 skip; 脚本内部失败必须作为错误抛到测试里。
    assert _require_built_app(build_dir) == build_dir / "bf_app"

    failing_python = tmp_path / "failing-python"
    failing_python.write_text("#!/usr/bin/env bash\nexit 3\n", encoding="utf-8")
    failing_python.chmod(0o755)

    with pytest.raises(subprocess.CalledProcessError) as excinfo:
        _run_script(build_dir, str(failing_python))
    assert excinfo.value.returncode == 3


def test_m1_foundation_verify_script_exists_and_supports_fast_mode():
    assert SCRIPT.exists()

    # 脚本以仓库根目录为工作目录启动, 相对 BF_NATIVE_BUILD_DIR 也按它展开。
    build_dir = _build_dir(cwd=ROOT)
    _require_built_app(build_dir)

    # 内层是 ``pytest tests --ignore=tests/scripts`` 的嵌套回归, 用当前解释器保证
    # 它有 pytest 与测试依赖; 授权门禁由 _script_env 排掉 (外层已单独跑)。
    result = _run_script(build_dir, sys.executable)
    assert "M1 foundation verification passed" in result.stdout
