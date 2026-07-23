"""M8 离线授权门禁测试。

验证:
1. CMake ``BF_REQUIRE_LICENSE=ON`` 且公钥文件缺失时 configure 非零退出
   (FATAL_ERROR)。
2. ``BF_REQUIRE_LICENSE=ON`` 且合法公钥时, CMake configure 成功,
   生成的嵌入头仅含公钥 (无私钥), 随后构建 ``bf_app`` 成功, 运行
   ``--smoke-license-gate`` 退出 0 且输出 ``smoke-license-gate-ok``。

测试在临时 build 目录内完成 configure→build→run 全链路, 不依赖任何
预存在的构建产物。cmake/openssl 不可用时以环境 skip 跳过。

不做源码文本断言; 只验证可观察的 CMake configure 退出码与 CLI 行为。
"""

import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / "native"


def _existing_qt_path() -> str:
    """从现有 build 目录的 CMakeCache 中提取 CMAKE_PREFIX_PATH, 避免重新发现 Qt。"""
    for candidate in (NATIVE / "build", NATIVE / "build-lic"):
        cache = candidate / "CMakeCache.txt"
        if cache.exists():
            for line in cache.read_text("utf-8").splitlines():
                if line.startswith("CMAKE_PREFIX_PATH"):
                    # format: CMAKE_PREFIX_PATH:PATH=...
                    return line.split("=", 1)[1].strip() if "=" in line else ""
    return ""


# -- CMake configure: BF_REQUIRE_LICENSE=ON + missing key → FATAL_ERROR ------

def test_cmake_require_license_fails_without_public_key(tmp_path):
    """``BF_REQUIRE_LICENSE=ON`` 且 ``BF_LICENSE_PUBLIC_KEY_FILE`` 缺失时,
    CMake configure 必须以非零退出码失败 (FATAL_ERROR)。

    这是发布门禁的核心: 不允许在无公钥的情况下产出 license-enabled 构建。
    """
    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("cmake 不可用")

    build = tmp_path / "build-nolic"
    build.mkdir()
    r = subprocess.run(
        [
            cmake,
            "-S", str(NATIVE),
            "-B", str(build),
            "-DBF_REQUIRE_LICENSE=ON",
            "-DBF_LICENSE_PUBLIC_KEY_FILE=",  # explicitly empty
            f"-DCMAKE_PREFIX_PATH={_existing_qt_path()}",
        ],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert r.returncode != 0, (
        f"BF_REQUIRE_LICENSE=ON + 空公钥路径应 FATAL_ERROR, 但 configure 成功:\n"
        f"stdout: {r.stdout}\nstderr: {r.stderr}"
    )
    assert re.search(r"BF_REQUIRE_LICENSE|public.?key|PEM", r.stderr + r.stdout, re.IGNORECASE), (
        f"FATAL_ERROR 消息应提及 BF_REQUIRE_LICENSE/公钥:\n{r.stderr}"
    )


# -- Configure + build + run --smoke-license-gate (full chain in tmp_path) --

def test_cmake_require_license_configures_builds_and_runs_smoke_gate(tmp_path):
    """``BF_REQUIRE_LICENSE=ON`` + 合法 RSA-2048 公钥的完整门禁链路:

    1. CMake configure 成功。
    2. 生成的嵌入头仅含 SubjectPublicKeyInfo PEM (无 PRIVATE KEY)。
    3. 构建 ``bf_app`` 目标成功 (BF_REQUIRE_LICENSE 编译定义生效)。
    4. 以 offscreen Qt + BF_RESOURCE_ROOT 运行 ``--smoke-license-gate``,
       退出 0 且输出 ``smoke-license-gate-ok``。
    """
    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("cmake 不可用")
    openssl = shutil.which("openssl")
    if openssl is None:
        pytest.skip("openssl 不可用")

    # 生成一个最小 RSA-2048 SubjectPublicKeyInfo PEM 公钥 (不含私钥)
    priv = tmp_path / "tmp_key.pem"
    key_path = tmp_path / "test_pubkey.pem"
    r = subprocess.run(
        [openssl, "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(priv)],
        capture_output=True, text=True, timeout=30,
    )
    if r.returncode != 0:
        pytest.skip(f"openssl 生成 RSA 密钥失败: {r.stderr}")
    r = subprocess.run(
        [openssl, "rsa", "-pubout", "-in", str(priv), "-out", str(key_path)],
        capture_output=True, text=True, timeout=30,
    )
    if r.returncode != 0:
        pytest.skip(f"openssl 提取公钥失败: {r.stderr}")
    # 删除私钥——测试不残留私钥
    priv.unlink()

    build = tmp_path / "build-lic"
    build.mkdir()

    # 1. configure 成功
    r = subprocess.run(
        [
            cmake,
            "-S", str(NATIVE),
            "-B", str(build),
            "-DBF_REQUIRE_LICENSE=ON",
            f"-DBF_LICENSE_PUBLIC_KEY_FILE={key_path}",
            f"-DCMAKE_PREFIX_PATH={_existing_qt_path()}",
        ],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert r.returncode == 0, (
        f"BF_REQUIRE_LICENSE=ON + 合法公钥应 configure 成功:\n"
        f"stdout: {r.stdout}\nstderr: {r.stderr}"
    )

    # 2. 生成的头文件仅含公钥 (无私钥), 且含 BEGIN PUBLIC KEY
    header = build / "bf_license_public_key_embed.h"
    assert header.exists(), f"配置应生成嵌入公钥头文件: {header}"
    content = header.read_text("utf-8")
    assert "PRIVATE KEY" not in content, "生成的公钥头文件不得含私钥"
    assert "BEGIN PUBLIC KEY" in content, "生成的公钥头文件应含 BEGIN PUBLIC KEY"

    # 3. 构建 bf_app 目标成功
    r = subprocess.run(
        [cmake, "--build", str(build), "--target", "bf_app", "-j"],
        capture_output=True,
        text=True,
        timeout=300,
    )
    assert r.returncode == 0, (
        f"构建 bf_app 目标应成功:\nstdout: {r.stdout}\nstderr: {r.stderr}"
    )
    app = build / "bf_app"
    assert app.exists(), f"构建应产出 bf_app 可执行文件: {app}"

    # 4. 运行 --smoke-license-gate, 退出 0 且输出 smoke-license-gate-ok
    env = {
        **os.environ,
        "QT_QPA_PLATFORM": "offscreen",
        "BF_RESOURCE_ROOT": str(ROOT),
    }
    r = subprocess.run(
        [str(app), "--smoke-license-gate"],
        cwd=ROOT, env=env,
        capture_output=True, text=True, timeout=60,
    )
    assert r.returncode == 0, (
        f"--smoke-license-gate 应退出 0, exit={r.returncode}\n"
        f"stdout: {r.stdout}\nstderr: {r.stderr}"
    )
    assert "smoke-license-gate-ok" in r.stdout, (
        f"应输出 smoke-license-gate-ok, stdout={r.stdout} stderr={r.stderr}"
    )
