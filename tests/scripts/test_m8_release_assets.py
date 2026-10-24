"""M8 发布资产可观察配置语义验证。

解析 PyInstaller spec、Inno Setup、build_release.ps1、法务资产与发布
资产校验脚本的配置语义, 不做整段文本快照。每条断言守护一条外部可观察
发布契约。
"""

import importlib.util
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "sidecar" / "boundfree_sidecar.spec"
ISS = ROOT / "installer" / "BoundFreeEEG.iss"
PS1 = ROOT / "scripts" / "windows" / "build_release.ps1"
CHECK = ROOT / "scripts" / "common" / "check_release_assets.py"
LEGAL = ROOT / "legal"
VERSION = ROOT / "version.json"


def _text(p: Path) -> str:
    return p.read_text(encoding="utf-8")


# -- PyInstaller spec --------------------------------------------------------

class TestSpec:
    def test_collect_all_mne(self):
        assert re.search(r"collect_all\s*\(\s*['\"]mne['\"]\s*\)", _text(SPEC))

    def test_onedir_mode(self):
        t = _text(SPEC)
        assert "exclude_binaries=True" in t and re.search(r"\bCOLLECT\s*\(", t)

    def test_version_and_contracts_as_datas(self):
        t = _text(SPEC)
        assert "version.json" in t and "error_codes.json" in t

    def test_matplotlib_not_excluded(self):
        m = re.search(r"excludes\s*=\s*\[([^\]]*)\]", _text(SPEC), re.DOTALL)
        if m:
            excluded = [s.strip().strip("'\"") for s in m.group(1).split(",") if s.strip()]
            assert "matplotlib" not in excluded

    def test_entry_point_referenced(self):
        t = _text(SPEC)
        assert "frozen_main.py" in t or "__main__.py" in t


# -- Inno Setup --------------------------------------------------------------

class TestIss:
    def test_disable_dir_page_no(self):
        m = re.search(r"^DisableDirPage\s*=\s*(\S+)", _text(ISS), re.MULTILINE)
        assert m and m.group(1).strip() == "no"

    def test_win10_x64(self):
        t = _text(ISS)
        m = re.search(r"^MinVersion\s*=\s*([\d.]+)", t, re.MULTILINE)
        assert m and int(m.group(1).split(".")[0]) >= 10
        m = re.search(r"^ArchitecturesInstallIn64BitMode\s*=\s*(\S+)", t, re.MULTILINE)
        assert m and "x64" in m.group(1).lower()

    def test_legal_source_and_version_define(self):
        t = _text(ISS)
        assert re.search(r"Source:.*legal", t, re.IGNORECASE)
        assert re.search(r"#define\s+BoundFreeVersion\b", t)
        assert "{#BoundFreeVersion}" in t

    def test_setup_section_directives_and_version_info(self):
        """``[Setup]`` 必须存在且位于第一个 directive 之前; AppId/AppName/
        PrivilegesRequired=admin 必须声明; VersionInfoVersion 与
        VersionInfoProductVersion 各只出现一次且绑定到
        ``{#BoundFreeFileVersion}``; 不得残留 ParseSemVer。"""
        t = _text(ISS)
        pos = t.find("[Setup]")
        assert pos != -1, "iss 必须包含 [Setup] 段"
        d = re.search(r"^(App|Version|Priv|Default|Arch|Disable|Uninst|Comp|Solid|Wizard|Output)\w*\s*=", t, re.MULTILINE)
        assert not d or pos < d.start(), f"[Setup] 必须在 directive ({d.group()}) 前"
        assert re.search(r"^AppId\s*=", t, re.MULTILINE), "缺 AppId"
        assert re.search(r"^AppName\s*=", t, re.MULTILINE), "缺 AppName"
        m = re.search(r"^PrivilegesRequired\s*=\s*(\w+)", t, re.MULTILINE)
        assert m and m.group(1) == "admin", f"PrivilegesRequired 必须=admin, 实际={m.group(1) if m else '缺失'}"
        assert re.search(r"#define\s+BoundFreeFileVersion\b", t), "缺 BoundFreeFileVersion 宏"
        for key in ("VersionInfoVersion", "VersionInfoProductVersion"):
            assert len(re.findall(rf"^{key}\s*=", t, re.MULTILINE)) == 1, f"{key} 只能出现一次"
            line = re.search(rf"^{key}\s*=\s*(.+)$", t, re.MULTILINE)
            assert line and "BoundFreeFileVersion" in line.group(1), f"{key} 必须用 BoundFreeFileVersion"
        assert "ParseSemVer" not in t, "iss 不得残留 ParseSemVer"
        m = re.search(r"^SourceDir\s*=\s*(.+)$", t, re.MULTILINE)
        assert m and m.group(1).strip() == "..", (
            f"SourceDir 必须 = .. (使 Source release\\stage 与 OutputDir dist 落在仓库根), "
            f"实际: {m.group(1) if m else '缺失'}"
        )


# -- build_release.ps1 -------------------------------------------------------

class TestPs1:
    def test_stable_rc_requires_signing_or_throw(self):
        t = _text(PS1)
        assert "RequiresSigning" in t
        assert re.search(r"['\"]stable['\"].*['\"]rc['\"]|['\"]rc['\"].*['\"]stable['\"]", t, re.IGNORECASE)
        assert re.search(r"throw.*[Ss]ign|throw.*[Cc]ert", t)

    @pytest.mark.parametrize("ch,req", [("stable", True), ("rc", True), ("dev", False), ("alpha", False), ("beta", False)])
    def test_signing_policy_channels(self, ch, req):
        t = _text(PS1)
        m = re.search(r'RequiresSigning\s*=\s*\S*?\$ReleaseChannel\s*-in\s*@?\(([^)]*)\)', t)
        assert m, "无法解析 RequiresSigning 通道集合"
        channels = [c.strip().strip("'\"") for c in m.group(1).split(",") if c.strip()]
        assert (ch in channels) == req

    def test_signs_all_targets_and_verifies(self):
        t = _text(PS1)
        for target in ("bf_app.exe", "boundfree_sidecar.exe", "BoundFreeEEG_Setup"):
            assert target in t
        assert re.search(r"verify.*\/pa", t)

    def test_stages_assets(self):
        t = _text(PS1)
        for kw in ("version.json", "error_codes.json", "legal", "i18n"):
            assert kw in t.lower() or kw in t
        assert ".qm" in t
        assert not re.search(r"Copy-Item.*i18n[\\/]\*\.ts", t)
    def test_no_dev_venv_in_staging(self):
        t = _text(PS1)
        for pat in (r"Copy-Item.*\.venv", r"Stage.*venv"):
            assert not re.search(pat, t, re.IGNORECASE)

    def test_uses_spec_windeployqt_iscc(self):
        t = _text(PS1)
        assert "boundfree_sidecar.spec" in t
        assert re.search(r"windeployqt", t, re.IGNORECASE)
        assert re.search(r"ISCC|iscc\.exe", t, re.IGNORECASE)

    def test_repo_root_resolution(self):
        """仓库根目录计算必须基于 $PSScriptRoot 向上两级 (Join-Path $PSScriptRoot '..\\..') 并经 Resolve-Path 解析，
        不得再使用错误的 $MyInvocation.MyCommand.Definition 双 Split-Path 形式。
        """
        t = _text(PS1)
        assert "$PSScriptRoot" in t, "build_release.ps1 必须使用 $PSScriptRoot 计算根目录"
        assert re.search(r"Join-Path\s+\$PSScriptRoot\s+[\"']\.\.[\/\\]\.\.[\"']", t), (
            "仓库根目录计算必须使用 Join-Path $PSScriptRoot '..\\..'"
        )
        assert re.search(r"Resolve-Path\s+.*\$PSScriptRoot", t), (
            "仓库根目录计算必须包含 Resolve-Path"
        )
        assert "$MyInvocation.MyCommand.Definition" not in t, (
            "不得再使用 $MyInvocation.MyCommand.Definition 提取脚本路径"
        )
        assert not re.search(r"Split-Path\s+-Parent\s*\(\s*Split-Path\s+-Parent\s+\$MyInvocation", t, re.IGNORECASE), (
            "不得使用 Split-Path -Parent (Split-Path -Parent $MyInvocation...) 双级父路径计算形式"
        )


# -- Legal assets ------------------------------------------------------------

class TestLegal:
    def test_required_files_exist(self):
        if not LEGAL.exists():
            pytest.skip("legal/ 尚未生成")
        for f in ("LICENSE.txt", "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_LICENSES.md"):
            assert (LEGAL / f).exists(), f"缺少 {f}"

    @pytest.mark.parametrize("dep", ["PySide6", "pyqtgraph", "cryptography"])
    def test_not_declared_as_shipped(self, dep):
        if not LEGAL.exists():
            pytest.skip("legal/ 尚未生成")
        for name in ("LICENSE.txt", "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_LICENSES.md"):
            p = LEGAL / name
            if not p.exists():
                continue
            for m in re.finditer(rf"\b{re.escape(dep)}\b", p.read_text("utf-8")):
                ctx = p.read_text("utf-8")[max(0, m.start() - 80):m.end() + 40].lower()
                if re.search(r"not\s+(bundle|ship|include)|does\s+not|legacy|not\s+part", ctx):
                    continue
                pytest.fail(f"{name} 在随包语境声明了 {dep}: ...{ctx}...")

    def test_acknowledges_real_dependency(self):
        if not LEGAL.exists():
            pytest.skip("legal/ 尚未生成")
        p = LEGAL / "THIRD_PARTY_NOTICES.md"
        if not p.exists():
            pytest.skip("THIRD_PARTY_NOTICES.md 尚未生成")
        t = p.read_text("utf-8").lower()
        assert any(d in t for d in ("qt6", "pocketfft", "mne", "numpy", "h5py"))

    def test_no_private_key_material(self):
        if not LEGAL.exists():
            pytest.skip("legal/ 尚未生成")
        for p in LEGAL.rglob("*"):
            if p.is_file():
                assert not re.search(r"BEGIN\s+(RSA\s+)?PRIVATE\s+KEY", p.read_text("utf-8", "replace"))


# -- Release check script ----------------------------------------------------

@pytest.fixture
def licensing_tree(tmp_path, monkeypatch):
    """Exercise release checks against isolated, deliberately damaged legal assets."""
    spec = importlib.util.spec_from_file_location("bf_licensing_asset_check", CHECK)
    module = importlib.util.module_from_spec(spec)
    monkeypatch.setitem(sys.modules, spec.name, module)
    spec.loader.exec_module(module)
    for relative in ("LICENSE", "NOTICE", "legal/LICENSE.txt", "legal/NOTICE.txt",
                     "legal/LICENSING.md", "legal/THIRD_PARTY_LICENSES.md", "legal/THIRD_PARTY_NOTICES.md",
                     "legal/licenses/LGPL-3.0.txt", "legal/licenses/GPL-3.0.txt"):
        target = tmp_path / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / relative, target)
    monkeypatch.setattr(module, "REPO_ROOT", tmp_path)
    return module, tmp_path


class TestCoreLicensing:
    def test_current_licensing_assets_pass(self, licensing_tree):
        checker, _ = licensing_tree
        assert checker.check_core_license().ok
        assert checker.check_legal().ok

    @pytest.mark.parametrize("relative", ["LICENSE", "NOTICE", "legal/LICENSING.md"])
    def test_missing_license_or_policy_fails(self, licensing_tree, relative):
        checker, root = licensing_tree
        (root / relative).unlink()
        result = checker.check_core_license()
        assert not result.ok and relative in result.detail

    @pytest.mark.parametrize("relative", ["legal/LICENSE.txt", "legal/NOTICE.txt"])
    def test_packaged_legal_drift_fails(self, licensing_tree, relative):
        checker, root = licensing_tree
        with (root / relative).open("a", encoding="utf-8") as output:
            output.write("\nUnexpected packaging change\n")
        result = checker.check_core_license()
        assert not result.ok and relative in result.detail

    def test_truncated_license_fails_even_when_copies_match(self, licensing_tree):
        checker, root = licensing_tree
        for relative in ("LICENSE", "legal/LICENSE.txt"):
            (root / relative).write_text("Apache License\nVersion 2.0, January 2004\n", encoding="utf-8")
        result = checker.check_core_license()
        assert not result.ok and "section" in result.detail

    def test_old_proprietary_core_declaration_fails(self, licensing_tree):
        checker, root = licensing_tree
        with (root / "legal/THIRD_PARTY_LICENSES.md").open("a", encoding="utf-8") as output:
            output.write("\nThe host uses the BoundFree-proprietary license.\n")
        result = checker.check_legal()
        assert not result.ok and "obsolete proprietary core" in result.detail

    @pytest.mark.parametrize("filename", ["LGPL-3.0.txt", "GPL-3.0.txt"])
    def test_missing_qt_license_fails(self, licensing_tree, filename):
        checker, root = licensing_tree
        (root / "legal/licenses" / filename).unlink()
        result = checker.check_legal()
        assert not result.ok and filename in result.detail

    @pytest.mark.parametrize("filename", ["LGPL-3.0.txt", "GPL-3.0.txt"])
    def test_qt_license_summary_or_modified_terms_fail(self, licensing_tree, filename):
        checker, root = licensing_tree
        path = root / "legal/licenses" / filename
        original = path.read_text(encoding="utf-8")
        for damaged in ("\n".join(original.splitlines()[:8]),
                        original.replace("Everyone is permitted", "Nobody is permitted", 1)):
            assert damaged != original
            path.write_text(damaged, encoding="utf-8")
            result = checker.check_legal()
            assert not result.ok and filename in result.detail

    @pytest.mark.parametrize("line_ending", [b"\n", b"\r\n"], ids=["LF", "CRLF"])
    def test_license_line_endings_pass(self, licensing_tree, line_ending):
        checker, root = licensing_tree
        for filename in ("LGPL-3.0.txt", "GPL-3.0.txt"):
            path = root / "legal/licenses" / filename
            normalized = path.read_text(encoding="utf-8").encode("utf-8")
            path.write_bytes(normalized.replace(b"\n", line_ending))
        assert checker.check_legal().ok


class TestCheckScript:
    def test_exists_and_has_run_all(self):
        assert CHECK.exists()
        assert "def run_all" in _text(CHECK)

    def test_validates_spec_iss_keys_privatekeys_signing(self):
        t = _text(CHECK)
        assert "collect_all" in t or "mne" in t
        assert "DisableDirPage" in t
        assert "PRIVATE" in t.upper() or "keygen" in t.lower()
        assert "signing" in t.lower() or "check_signing" in t

    def test_signing_policy_stable_true_dev_false(self):
        if not CHECK.exists():
            pytest.skip("check 脚本不存在")
        r = subprocess.run(
            [sys.executable, "-c",
             "import sys; sys.path.insert(0,'scripts/common'); "
             "from check_release_assets import check_signing_policy; "
             "print(check_signing_policy('stable').detail); "
             "print(check_signing_policy('dev').detail)"],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        assert r.returncode == 0, r.stderr
        assert "signing_required=True" in r.stdout
        assert "signing_required=False" in r.stdout


# -- Private key / keygen guard ----------------------------------------------

SKIP_DIRS = {".git", "build", "build-windows", "__pycache__", ".pytest_cache",
             ".venv", "node_modules", ".qt", "release", "dist", "_internal"}
SKIP_EXT = {".png", ".jpg", ".jpeg", ".qm", ".bin", ".eeg", ".edf", ".csv",
            ".npy", ".hdf5", ".h5", ".fif", ".set", ".vhdr", ".vmrk", ".bdf",
            ".o", ".so", ".dll", ".exe", ".pyc", ".cache"}

class TestNoSecrets:
    def test_no_private_keys(self):
        hits = []
        for p in ROOT.rglob("*"):
            if not p.is_file() or any(d in p.parts for d in SKIP_DIRS) or p.suffix.lower() in SKIP_EXT:
                continue
            if re.search(rb"-----BEGIN\s+(RSA|EC|OPENSSH|PGP)?\s*PRIVATE\s+KEY-----", p.read_bytes()):
                hits.append(str(p.relative_to(ROOT)))
        assert not hits, f"私钥材料: {hits}"

    def test_no_keygen_files(self):
        hits = [str(p.relative_to(ROOT)) for p in ROOT.rglob("*")
                if p.is_file() and not any(d in p.parts for d in SKIP_DIRS)
                and p.suffix.lower() not in SKIP_EXT
                and any(h in p.name.lower() for h in ("keygen", "key_gen", "generate_key", "genkey"))]
        assert not hits, f"keygen 脚本: {hits}"


# -- version.json consistency ------------------------------------------------

class TestVersionJson:
    def test_well_formed(self):
        d = json.loads(VERSION.read_text("utf-8"))
        for k in ("schema_version", "product_name", "version", "release_channel", "ipc_protocol_version"):
            assert k in d and d[k] not in (None, ""), f"缺失/空: {k}"
        assert re.fullmatch(r"\d+\.\d+\.\d+([A-Za-z0-9.+-]*)", str(d["version"]))
        assert d["release_channel"] in ("dev", "alpha", "beta", "rc", "stable")

    def test_iss_default_version_matches(self):
        m = re.search(r'#define\s+BoundFreeVersion\s+"([^"]*)"', _text(ISS))
        assert m and m.group(1) == json.loads(VERSION.read_text("utf-8"))["version"]
