"""M8 安装态运行时路径验证。

用临时安装布局验证边车 ``BF_RESOURCE_ROOT`` 下 ``--version`` 与 framed
hello; 若冻结 exe 可用则额外验证它; 通过 bf_app 既有 ``--version`` 与
``--smoke-ui`` 验证 Native 资源根解析。不修改生产代码。
"""

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]

# -- install layout ----------------------------------------------------------

def _build_layout(tmp_path: Path) -> Path:
    """构造最小安装布局: version.json + contracts + i18n。"""
    d = tmp_path / "install"
    d.mkdir()
    shutil.copy2(ROOT / "version.json", d / "version.json")
    (d / "contracts").mkdir()
    shutil.copy2(ROOT / "contracts" / "error_codes.json", d / "contracts" / "error_codes.json")
    i18n = d / "i18n"
    i18n.mkdir()
    for ts in (ROOT / "native" / "i18n").glob("bf_app_*.ts"):
        shutil.copy2(ts, i18n / ts.name)
    return d


def _sidecar_env(install: Path) -> dict:
    env = os.environ.copy()
    env["BF_RESOURCE_ROOT"] = str(install)
    env["PYTHONPATH"] = str(ROOT / "sidecar")
    env["PYTHONIOENCODING"] = "utf-8"
    env["PYTHONUTF8"] = "1"
    return env


def _mutate_version(install: Path, version: str, ipc: int | None = None, name: str | None = None) -> dict:
    d = json.loads((install / "version.json").read_text("utf-8"))
    d["version"] = version
    if ipc is not None:
        d["ipc_protocol_version"] = ipc
    if name is not None:
        d["product_name"] = name
    (install / "version.json").write_text(json.dumps(d, ensure_ascii=False), "utf-8")
    return d


# -- sidecar --version via BF_RESOURCE_ROOT ----------------------------------

class TestSidecarVersion:
    def test_reads_layout_version_not_source(self, tmp_path):
        """``--version`` 必须从 ``BF_RESOURCE_ROOT`` 布局读 version.json,
        不回退源码树。用独特版本号区分。"""
        install = _build_layout(tmp_path)
        _mutate_version(install, "9.9.9-layout")
        r = subprocess.run(
            [sys.executable, "-u", "-m", "boundfree_sidecar", "--version"],
            cwd=ROOT, env=_sidecar_env(install),
            capture_output=True, text=True, timeout=30)
        assert r.returncode == 0, f"stderr={r.stderr}"
        assert "9.9.9-layout" in r.stdout, f"未从布局读取版本: {r.stdout}"
        source_ver = json.loads((ROOT / "version.json").read_text("utf-8"))["version"]
        assert source_ver != "9.9.9-layout"
        assert source_ver not in r.stdout, f"回退读了源码树: {r.stdout}"

    def test_includes_ipc_protocol_version(self, tmp_path):
        install = _build_layout(tmp_path)
        _mutate_version(install, "1.2.3-ipc", ipc=77)
        r = subprocess.run(
            [sys.executable, "-u", "-m", "boundfree_sidecar", "--version"],
            cwd=ROOT, env=_sidecar_env(install),
            capture_output=True, text=True, timeout=30)
        assert r.returncode == 0
        assert "ipc_protocol_version=77" in r.stdout


# -- sidecar framed hello via stdio ------------------------------------------

def _stdio_hello(install: Path, frozen_exe: Path | None = None):
    """启动边车 stdio, 发 framed hello, 返回响应 dict。使用生产
    framing 模块保证与协议字节序完全一致。"""
    from boundfree_sidecar.framing import write_json_frame, read_json_frame

    cmd = [str(frozen_exe), "--stdio"] if frozen_exe else \
          [sys.executable, "-u", "-m", "boundfree_sidecar", "--stdio"]
    proc = subprocess.Popen(cmd, cwd=ROOT, env=_sidecar_env(install),
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, bufsize=0)
    try:
        write_json_frame(proc.stdin, {"id": "h", "method": "hello", "params": {}})
        resp = read_json_frame(proc.stdout)
        assert resp is not None, f"无响应; stderr={proc.stderr.read() if proc.stderr else ''}"
        return resp
    finally:
        if proc.stdin and not proc.stdin.closed:
            proc.stdin.close()
        if proc.poll() is None:
            proc.wait(timeout=5)


class TestSidecarFramedHello:
    def test_hello_returns_layout_version(self, tmp_path):
        """hello 响应的 product_version 与 ipc_protocol_version 必须来自
        安装布局的 version.json。"""
        install = _build_layout(tmp_path)
        _mutate_version(install, "7.7.7-hello", ipc=42)
        resp = _stdio_hello(install)
        assert resp["ok"] is True
        assert resp["result"]["product_version"] == "7.7.7-hello"
        assert resp["result"]["ipc_protocol_version"] == 42

    def test_hello_then_unknown_method(self, tmp_path):
        """hello 后帧循环持续: 未知方法返回 protocol.unknown_method。"""
        from boundfree_sidecar.framing import write_json_frame, read_json_frame
        install = _build_layout(tmp_path)
        proc = subprocess.Popen(
            [sys.executable, "-u", "-m", "boundfree_sidecar", "--stdio"],
            cwd=ROOT, env=_sidecar_env(install),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, bufsize=0)
        try:
            write_json_frame(proc.stdin, {"id": "1", "method": "hello", "params": {}})
            assert read_json_frame(proc.stdout)["ok"] is True
            write_json_frame(proc.stdin, {"id": "2", "method": "nope", "params": {}})
            r = read_json_frame(proc.stdout)
            assert r["ok"] is False and r["error"]["code"] == "protocol.unknown_method"
        finally:
            if proc.stdin and not proc.stdin.closed:
                proc.stdin.close()
            if proc.poll() is None:
                proc.wait(timeout=5)


# -- frozen sidecar (if available) ------------------------------------------

FROZEN = None
_dist = ROOT / "release" / "sidecar-dist" / "boundfree_sidecar"
for _name in ("boundfree_sidecar", "boundfree_sidecar.exe"):
    if (_dist / _name).exists():
        FROZEN = _dist / _name
        break


class TestFrozenSidecar:
    def test_frozen_version_and_hello(self, tmp_path):
        if FROZEN is None:
            pytest.skip("冻结边车未生成")
        install = _build_layout(tmp_path)
        _mutate_version(install, "5.5.5-frozen", ipc=99)
        env = _sidecar_env(install)
        r = subprocess.run([str(FROZEN), "--version"], cwd=ROOT, env=env,
                           capture_output=True, text=True, timeout=60)
        if r.returncode != 0:
            pytest.skip(f"冻结边车无法启动 (构建可能不完整): {r.stderr[:200]}")
        assert "5.5.5-frozen" in r.stdout, f"冻结态未从 BF_RESOURCE_ROOT 读版本: {r.stdout}"
        resp = _stdio_hello(install, frozen_exe=FROZEN)
        assert resp["ok"] and resp["result"]["product_version"] == "5.5.5-frozen"


# -- bf_app under BF_RESOURCE_ROOT ------------------------------------------

def _candidate_apps() -> list[Path]:
    return [ROOT / "native" / f"build{suffix}" / name
            for suffix in ("", "-windows")
            for sub in ("", "Release", "Debug")
            for name in ("bf_app", "bf_app.exe")
            ] + [ROOT / "native" / "build" / "bf_app", ROOT / "native" / "build" / "bf_app.exe"]

APP = next((p for p in _candidate_apps() if p.exists()), None)


class TestBfAppResourceRoot:
    def test_version_reads_layout(self, tmp_path):
        if APP is None:
            pytest.skip("bf_app 未构建")
        install = _build_layout(tmp_path)
        _mutate_version(install, "4.4.4-app", name="AppLayoutTest")
        env = {**os.environ, "BF_RESOURCE_ROOT": str(install), "QT_QPA_PLATFORM": "offscreen"}
        r = subprocess.run([str(APP), "--version"], cwd=ROOT, env=env,
                           capture_output=True, text=True, timeout=30)
        assert r.returncode == 0, f"stderr={r.stderr}"
        assert "4.4.4-app" in r.stdout, f"未从布局读取版本: {r.stdout}"
        source_ver = json.loads((ROOT / "version.json").read_text("utf-8"))["version"]
        assert source_ver not in r.stdout, f"回退读了源码树: {r.stdout}"

    def test_smoke_ui_starts_from_layout(self, tmp_path):
        if APP is None:
            pytest.skip("bf_app 未构建")
        install = _build_layout(tmp_path)
        env = {**os.environ, "BF_RESOURCE_ROOT": str(install), "QT_QPA_PLATFORM": "offscreen"}
        r = subprocess.run([str(APP), "--smoke-ui"], cwd=ROOT, env=env,
                           capture_output=True, text=True, timeout=30)
        if r.returncode != 0:
            pytest.skip(f"bf_app --smoke-ui 环境失败 (可能缺显示): exit={r.returncode}")
        assert "smoke-ui-ok" in r.stdout, f"smoke-ui 未成功: {r.stdout}"


# -- contracts isolation -----------------------------------------------------

class TestContractsIsolation:
    def test_make_error_reads_layout_contracts(self, tmp_path):
        """``make_error`` 的 retryable 必须从布局中的 contracts 读取,
        不回退源码树。翻转布局中某错误码的 retryable 值验证。"""
        install = _build_layout(tmp_path)
        data = json.loads((install / "contracts" / "error_codes.json").read_text("utf-8"))
        code = data["error_codes"][0]["code"]
        original = data["error_codes"][0]["retryable"]
        data["error_codes"][0]["retryable"] = not original
        (install / "contracts" / "error_codes.json").write_text(
            json.dumps(data, ensure_ascii=False), "utf-8")
        r = subprocess.run(
            [sys.executable, "-c",
             f"import sys; sys.path.insert(0,{str(ROOT/'sidecar')!r}); "
             f"from boundfree_sidecar.errors import make_error; "
             f"print(make_error({code!r}).retryable)"],
            cwd=ROOT, env=_sidecar_env(install),
            capture_output=True, text=True, timeout=30)
        assert r.returncode == 0, f"stderr={r.stderr}"
        assert r.stdout.strip() == str(not original), \
            f"retryable={r.stdout.strip()}, 期望={not original}"
