#!/usr/bin/env python3
"""Static release-asset validator for BoundFree EEG Native.

This script parses (not merely greps) the packaging artifacts and asserts:

  * version.json is well-formed and carries every required field.
  * sidecar/boundfree_sidecar.spec uses onedir, collect_all('mne'), keeps
    matplotlib bundled, and ships version.json + contracts/error_codes.json.
  * installer/BoundFreeEEG.iss is Windows 10 x64, DisableDirPage=no, binds
    AppVersion to the injected define, and stages legal assets.
  * Legal assets acknowledge shipped dependencies and do NOT declare
    unshipped dependencies (PySide6/pyqtgraph/cryptography).
  * Qt's LGPL v3 and accompanying GPL v3 texts match complete upstream copies.
  * Apache-2.0 LICENSE and NOTICE match their packaged copies, and the
    licensing/extension policy is present.
  * No private key material or keygen script is present in the tree.

Exit code 0 = all checks pass; 1 = one or more failures. Run from the repo
root: ``python3 scripts/common/check_release_assets.py``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

REPO_ROOT = Path(__file__).resolve().parents[2]

# Dependencies this release does NOT ship. Declaring them in legal assets
# would be a leftover from the old PySide6 reference build.
FORBIDDEN_LEGAL_DEPS = ("PySide6", "pyqtgraph", "cryptography")

# Transitive dependencies that ARE bundled (via MNE) and therefore MUST be
# acknowledged in the legal notices + license texts. Omitting them would
# under-disclose; over-declaring PySide6/pyqtgraph/cryptography would
# mis-attribute the old reference stack.
REQUIRED_ACKNOWLEDGED_DEPS = (
    "Qt6",
    "pocketfft",
    "Microsoft Visual C++ Redistributable",
    "MNE",
    "numpy",
    "h5py",
    "matplotlib",
    "scipy",
    "pandas",
    "Neo",
    "quantities",
)

# Verbatim license files from Qt's pinned v6.8.2 source tree:
# https://github.com/qt/qtbase/tree/v6.8.2/LICENSES
# Normalize line endings for Windows checkouts, but reject edited/truncated text.
QT_LICENSE_SHA256 = {
    "LGPL-3.0.txt": "da7eabb7bafdf7d3ae5e9f223aa5bdc1eece45ac569dc21b3b037520b4464768",
    "GPL-3.0.txt": "8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903",
}

# Patterns that indicate private key material or keygen tooling. The public
# license-signing key resource is allowed; private keys and key generators
# are release-blocking.
PRIVATE_KEY_PATTERNS = (
    r"-----BEGIN\s+(RSA|EC|OPENSSH|PGP)?\s*PRIVATE\s+KEY-----",
    r"-----BEGIN\s+ENCRYPTED\s+PRIVATE\s+KEY-----",
    r"-----BEGIN\s+PRIVATE\s+KEY-----",
)
KEYGEN_FILE_HINTS = ("keygen", "key_gen", "generate_key", "genkey")


@dataclass
class CheckResult:
    name: str
    ok: bool
    detail: str = ""

    def format(self) -> str:
        status = "PASS" if self.ok else "FAIL"
        line = f"[{status}] {self.name}"
        if self.detail:
            line += f"\n        {self.detail}"
        return line


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


# ---------------------------------------------------------------------------
# version.json
# ---------------------------------------------------------------------------

def check_version_json() -> CheckResult:
    name = "version.json well-formed and complete"
    path = REPO_ROOT / "version.json"
    if not path.exists():
        return CheckResult(name, False, f"missing: {path}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return CheckResult(name, False, f"invalid JSON: {exc}")
    required = ("schema_version", "product_name", "version", "release_channel", "ipc_protocol_version")
    missing = [k for k in required if k not in data or data[k] in (None, "")]
    if missing:
        return CheckResult(name, False, f"missing/empty fields: {missing}")
    if not re.fullmatch(r"\d+\.\d+\.\d+([A-Za-z0-9.+-]*)", str(data["version"])):
        return CheckResult(name, False, f"version not SemVer-ish: {data['version']}")
    if data["release_channel"] not in ("dev", "alpha", "beta", "rc", "stable"):
        return CheckResult(name, False, f"unknown channel: {data['release_channel']}")
    return CheckResult(name, True, f"v{data['version']} channel={data['release_channel']}")


def check_version_sync() -> CheckResult:
    name = "version metadata synchronized"
    try:
        import importlib.util

        script = REPO_ROOT / "scripts" / "common" / "version_sync.py"
        if not script.exists():
            return CheckResult(name, False, "missing: scripts/common/version_sync.py")
        spec = importlib.util.spec_from_file_location("bf_version_sync", script)
        if spec is None or spec.loader is None:
            return CheckResult(name, False, "cannot load scripts/common/version_sync.py")
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        failures = module.check_sync()
    except Exception as exc:  # pragma: no cover - failure is reported as release gate detail
        return CheckResult(name, False, f"version sync check crashed: {exc}")
    if failures:
        return CheckResult(name, False, "; ".join(failures))
    version = json.loads((REPO_ROOT / "version.json").read_text(encoding="utf-8"))["version"]
    return CheckResult(name, True, f"version={version}")


# ---------------------------------------------------------------------------
# PyInstaller spec
# ---------------------------------------------------------------------------

def check_sidecar_spec() -> CheckResult:
    name = "sidecar/boundfree_sidecar.spec"
    path = REPO_ROOT / "sidecar" / "boundfree_sidecar.spec"
    if not path.exists():
        return CheckResult(name, False, f"missing: {path}")
    src = _read_text(path)

    failures: list[str] = []

    if "collect_all('mne')" not in src and 'collect_all("mne")' not in src:
        failures.append("must call collect_all('mne')")
    if "collect_all('matplotlib')" not in src and 'collect_all("matplotlib")' not in src:
        # matplotlib may be collected transitively via mne; the hard rule is
        # that it must NOT appear in excludes.
        pass
    if re.search(r'^\s*"matplotlib"\s*,?\s*$', src, re.MULTILINE):
        failures.append("matplotlib must not be excluded")
    excludes_match = re.search(r"excludes\s*=\s*\[(.*?)\]", src, re.DOTALL)
    if excludes_match:
        if "matplotlib" in (excludes_match.group(1)):
            failures.append("matplotlib listed in excludes")

    if "exclude_binaries=True" not in src:
        failures.append("not onedir (exclude_binaries=True missing)")
    if "COLLECT(" not in src:
        failures.append("missing COLLECT block (not onedir)")

    if "version.json" not in src or "error_codes.json" not in src:
        failures.append("version.json / error_codes.json not staged as datas")
    if "frozen_main.py" not in src and "__main__.py" not in src:
        failures.append("entry point not referenced (frozen_main.py or __main__.py)")

    # Must not bundle unshipped heavy deps that bloat the installer.
    for forbidden in ("PySide6", "pyqtgraph", "cryptography"):
        if re.search(rf"collect_all\(['\"]{forbidden}['\"]\)", src):
            failures.append(f"collect_all({forbidden}) should not be bundled")

    if failures:
        return CheckResult(name, False, "; ".join(failures))
    return CheckResult(name, True, "onedir + collect_all('mne') + assets")


# ---------------------------------------------------------------------------
# Inno Setup .iss
# ---------------------------------------------------------------------------

def check_iss() -> CheckResult:
    name = "installer/BoundFreeEEG.iss"
    path = REPO_ROOT / "installer" / "BoundFreeEEG.iss"
    if not path.exists():
        return CheckResult(name, False, f"missing: {path}")
    src = _read_text(path)
    failures: list[str] = []
    setup_pos = src.find("[Setup]")
    first_directive_positions = [src.find(f"\n{name}=") for name in ("AppId", "AppName", "AppVersion")]
    first_directive_positions = [pos for pos in first_directive_positions if pos >= 0]
    if setup_pos < 0 or (first_directive_positions and setup_pos > min(first_directive_positions)):
        failures.append("[Setup] section missing or appears after setup directives")
    for directive in ("AppId=", "AppName=", "PrivilegesRequired=admin"):
        if directive not in src:
            failures.append(f"required setup directive missing: {directive}")
    if not re.search(r"^SourceDir\s*=\s*\.\.\s*$", src, re.MULTILINE):
        failures.append("SourceDir must be '..' so release/stage paths resolve from installer/")
    if src.count("VersionInfoVersion=") != 1 or src.count("VersionInfoProductVersion=") != 1:
        failures.append("VersionInfoVersion/ProductVersion must each appear exactly once")
    if "ParseSemVer" in src:
        failures.append("undefined ParseSemVer expression present")

    if not re.search(r"#define\s+BoundFreeVersion\b", src):
        failures.append("BoundFreeVersion define missing (version not parameterized)")
    if "AppVersion=" not in src or "{#BoundFreeVersion}" not in src:
        failures.append("AppVersion not bound to {#BoundFreeVersion} (SemVer display)")
    # VersionInfoVersion must use a numeric FileVersion define, NOT the SemVer
    # string (Inno Setup rejects "0.1.0-dev" here).
    if not re.search(r"#define\s+BoundFreeFileVersion\b", src):
        failures.append("BoundFreeFileVersion numeric define missing")
    if "VersionInfoVersion={#BoundFreeFileVersion}" not in src:
        failures.append("VersionInfoVersion must use numeric {#BoundFreeFileVersion}")
    if re.search(r"VersionInfoVersion=\{#BoundFreeVersion\}", src):
        failures.append("VersionInfoVersion illegally bound to SemVer {#BoundFreeVersion}")
    m = re.search(r"^DisableDirPage\s*=\s*(\w+)", src, re.MULTILINE)
    if not m:
        failures.append("DisableDirPage not declared")
    elif m.group(1).lower() != "no":
        failures.append(f"DisableDirPage={m.group(1)} must be 'no'")

    m = re.search(r"^ArchitecturesInstallIn64BitMode\s*=\s*(\w+)", src, re.MULTILINE)
    if not m or m.group(1) != "x64compatible":
        failures.append("ArchitecturesInstallIn64BitMode must be x64compatible")

    m = re.search(r"^MinVersion\s*=\s*([\d.]+)", src, re.MULTILINE)
    if not m:
        failures.append("MinVersion not declared")
    else:
        major = int(m.group(1).split(".")[0])
        if major < 10:
            failures.append(f"MinVersion={m.group(1)} below Windows 10")

    if "legal" not in src.lower():
        failures.append("legal assets not staged in iss")

    if failures:
        return CheckResult(name, False, "; ".join(failures))
    return CheckResult(name, True, "Win10 x64, DisableDirPage=no, version-bound, legal staged")


# ---------------------------------------------------------------------------
# Branding assets
# ---------------------------------------------------------------------------

def check_branding_assets() -> CheckResult:
    name = "native branding assets and packaging references"
    branding = REPO_ROOT / "native" / "assets" / "branding"
    required = (
        "BoundFree_LOGO_WithText.png",
        "BoundFree_LOGO_NoText.png",
        "BoundFree_app_icon.png",
        "BoundFree_app_icon.ico",
    )
    missing = [asset for asset in required if not (branding / asset).is_file()]
    if missing:
        return CheckResult(name, False, f"missing branding assets: {missing}")

    qrc = REPO_ROOT / "native" / "resources" / "branding.qrc"
    rc = REPO_ROOT / "native" / "resources" / "boundfree_app.rc"
    cmake = REPO_ROOT / "native" / "CMakeLists.txt"
    iss = REPO_ROOT / "installer" / "BoundFreeEEG.iss"
    required_files = (qrc, rc, cmake, iss)
    absent = [str(candidate.relative_to(REPO_ROOT)) for candidate in required_files if not candidate.is_file()]
    if absent:
        return CheckResult(name, False, f"missing branding configuration: {absent}")

    qrc_text = _read_text(qrc)
    rc_text = _read_text(rc)
    cmake_text = _read_text(cmake)
    iss_text = _read_text(iss)
    failures: list[str] = []
    for asset in ("BoundFree_LOGO_WithText.png", "BoundFree_LOGO_NoText.png", "BoundFree_app_icon.png"):
        if asset not in qrc_text:
            failures.append(f"branding.qrc does not embed {asset}")
    if "BoundFree_app_icon.ico" not in rc_text:
        failures.append("Windows resource file does not reference application ICO")
    if "target_sources(bf_app PRIVATE ${BF_BRANDING_QRC})" not in cmake_text:
        failures.append("bf_app does not embed branding qrc")
    if "target_sources(bf_app PRIVATE ${BF_WINDOWS_APP_ICON_RC})" not in cmake_text:
        failures.append("bf_app does not embed Windows application icon")
    if not re.search(r"^SetupIconFile\s*=\s*native\\assets\\branding\\BoundFree_app_icon\.ico\s*$", iss_text, re.MULTILINE):
        failures.append("SetupIconFile must reference the application ICO")
    if failures:
        return CheckResult(name, False, "; ".join(failures))
    return CheckResult(name, True, "embedded in bf_app and referenced by Windows installer")


# ---------------------------------------------------------------------------
# Legal assets
# ---------------------------------------------------------------------------

def check_core_license() -> CheckResult:
    name = "open core license and packaged notices synchronized"
    required = ("LICENSE", "NOTICE", "legal/LICENSE.txt", "legal/NOTICE.txt", "legal/LICENSING.md")
    missing = [path for path in required if not (REPO_ROOT / path).is_file()]
    if missing:
        return CheckResult(name, False, f"missing licensing files: {missing}")

    failures: list[str] = []
    for source, packaged in (("LICENSE", "legal/LICENSE.txt"), ("NOTICE", "legal/NOTICE.txt")):
        if (REPO_ROOT / source).read_bytes() != (REPO_ROOT / packaged).read_bytes():
            failures.append(f"{packaged} differs from {source}")
    license_text = _read_text(REPO_ROOT / "LICENSE")
    for marker in ("Apache License", "Version 2.0, January 2004", "3. Grant of Patent License.",
                   "9. Accepting Warranty or Additional Liability.", "END OF TERMS AND CONDITIONS"):
        if marker not in license_text:
            failures.append(f"LICENSE missing Apache-2.0 section: {marker}")
    if failures:
        return CheckResult(name, False, "; ".join(failures))
    return CheckResult(name, True, "Apache-2.0, NOTICE and extension policy included under legal/")


def check_legal() -> CheckResult:
    name = "legal assets present and dependency-accurate"
    root = REPO_ROOT / "legal"
    required = ("LICENSE.txt", "THIRD_PARTY_NOTICES.md", "THIRD_PARTY_LICENSES.md")
    missing = [f for f in required if not (root / f).exists()]
    if missing:
        return CheckResult(name, False, f"missing legal files: {missing}")
    failures: list[str] = []
    for filename, expected in QT_LICENSE_SHA256.items():
        path = root / "licenses" / filename
        if not path.is_file():
            failures.append(f"missing complete Qt license: licenses/{filename}")
        elif hashlib.sha256(_read_text(path).encode("utf-8")).hexdigest() != expected:
            failures.append(f"licenses/{filename} differs from complete upstream license text")
    # Strip explicit "not bundled" disclaimer lines so that a legal file may
    # legitimately state it does NOT ship a component without that being
    # flagged as a false declaration. A line counts as negated if it mentions
    # a forbidden component AND carries any negation cue.
    all_text = "\n\n".join(_read_text(root / f) for f in required)
    for obsolete in ("BoundFree-proprietary", "BoundFree proprietary host application"):
        if obsolete in all_text:
            failures.append(f"obsolete proprietary core declaration: {obsolete}")
    negation_cues = ("not bundle", "not ship", "does not", "not part", "not distributed", "are not", "were part of the legacy")
    negated_lines = set()
    for line in all_text.splitlines():
        low = line.lower()
        if any(cue in low for cue in negation_cues):
            negated_lines.add(line)
    decl_text = "\n".join(l for l in all_text.splitlines() if l not in negated_lines)
    # Forbidden (unshipped) deps must not appear as bundled/declared deps.
    for dep in FORBIDDEN_LEGAL_DEPS:
        if re.search(rf"\b{re.escape(dep)}\b", decl_text):
            failures.append(f"legal asset declares unshipped dep '{dep}'")
    # Required (shipped, incl. transitive) deps MUST be acknowledged in both
    # the notices table and the license index. This is not a binary license audit.
    notices = _read_text(root / "THIRD_PARTY_NOTICES.md")
    licenses = _read_text(root / "THIRD_PARTY_LICENSES.md")
    acknowledged_notices = [d for d in REQUIRED_ACKNOWLEDGED_DEPS if d.lower() in notices.lower()]
    acknowledged_licenses = [d for d in REQUIRED_ACKNOWLEDGED_DEPS if d.lower() in licenses.lower()]
    missing_notices = set(REQUIRED_ACKNOWLEDGED_DEPS) - set(acknowledged_notices)
    missing_licenses = set(REQUIRED_ACKNOWLEDGED_DEPS) - set(acknowledged_licenses)
    if missing_notices:
        failures.append(f"THIRD_PARTY_NOTICES.md missing acknowledgements: {sorted(missing_notices)}")
    if missing_licenses:
        failures.append(f"THIRD_PARTY_LICENSES.md missing license entries: {sorted(missing_licenses)}")
    if failures:
        return CheckResult(name, False, "; ".join(failures))
    return CheckResult(name, True, f"acknowledges all {len(REQUIRED_ACKNOWLEDGED_DEPS)} shipped deps; complete Qt LGPL/GPL texts")


# ---------------------------------------------------------------------------
# Private key / keygen guard
# ---------------------------------------------------------------------------

def check_no_private_keys() -> CheckResult:
    name = "no private key material or keygen scripts"
    hits: list[str] = []
    skip_dirs = {".git", "native", "build", "build-windows", "__pycache__", ".pytest_cache", ".venv", "node_modules", "release", "dist"}
    for path in REPO_ROOT.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(REPO_ROOT)
        if any(part in skip_dirs for part in rel.parts):
            continue
        if path.suffix.lower() in (".png", ".jpg", ".jpeg", ".qm", ".bin", ".eeg", ".edf", ".csv", ".npy", ".hdf5"):
            continue
        try:
            text = path.read_bytes()
        except OSError:
            continue
        for pat in PRIVATE_KEY_PATTERNS:
            if re.search(pat.encode(), text):
                hits.append(f"{rel}: private key material")
                break
        else:
            lower = rel.name.lower()
            if any(hint in lower for hint in KEYGEN_FILE_HINTS):
                hits.append(f"{rel}: keygen-like filename")
            else:
                try:
                    content = path.read_text(encoding="utf-8", errors="ignore")
                except OSError:
                    continue
                # Detect RSA key generation code (not the public-key verifier).
                if re.search(r"rsa\.generate_private_key|RSA\.generate|Crypto\.PublicKey\.RSA", content) and "public" not in lower:
                    hits.append(f"{rel}: RSA key generation code")
    if hits:
        return CheckResult(name, False, "; ".join(hits))
    return CheckResult(name, True, "no private keys / keygen detected")


# ---------------------------------------------------------------------------
# i18n release strategy (build_release.ps1): .qm-only runtime, no .ts staging
# ---------------------------------------------------------------------------

def check_i18n_release() -> CheckResult:
    """Validate that the release pipeline ships .qm and never .ts at runtime."""
    name = "i18n .qm-only release (no .ts staging)"
    path = REPO_ROOT / "scripts" / "windows" / "build_release.ps1"
    if not path.exists():
        return CheckResult(name, False, "build_release.ps1 missing")
    src = _read_text(path)
    failures: list[str] = []

    # 1. The stage must NOT copy native/i18n/*.ts into stage\i18n.
    if re.search(r"Copy-Item.*i18n[\\/]\*\.ts", src):
        failures.append("stage still copies native/i18n/*.ts (must use CMake-installed .qm)")

    # 2. The pipeline must assert stable app .qm files exist in stage. The
    #    script builds the filename via a foreach loop over zh_CN/en_US/ja_JP/zh_TW,
    #    so we require the locale list and the .qm existence guard (Test-Path +
    #    throw on missing).
    if not re.search(r'@\(\s*"zh_CN"\s*,\s*"en_US"\s*,\s*"ja_JP"\s*,\s*"zh_TW"\s*\)', src):
        failures.append("stable locale list @(\"zh_CN\",\"en_US\",\"ja_JP\",\"zh_TW\") not found")
    if not re.search(r"bf_app_\$\S*\.qm", src) and "bf_app_" not in src:
        failures.append("no bf_app_<locale>.qm path construction found")
    if "App translation missing" not in src:
        failures.append("missing throw-on-absent app .qm assertion")

    # 3. windeployqt must run with --no-translations (no bulk Qt locale copy);
    #    Qt base translations are staged explicitly from $QtRoot\translations.
    if "--no-translations" not in src:
        failures.append("windeployqt must pass --no-translations to avoid bulk Qt locale copy")
    if "translations" not in src or "qtbase_zh_CN.qm" not in src:
        failures.append("Qt base translation qtbase_zh_CN.qm not explicitly staged")
    if "qtbase_ja.qm" not in src or "qtbase_ja_JP.qm" not in src:
        failures.append("Qt base translation qtbase_ja.qm -> qtbase_ja_JP.qm rename not staged")

    # 4. If a stage tree exists from a prior build, assert it contains no .ts
    #    and carries the three stable app .qm files.
    stage = REPO_ROOT / "release" / "stage" / "BoundFreeEEG" / "i18n"
    if stage.is_dir():
        ts_files = sorted(p.name for p in stage.glob("*.ts"))
        if ts_files:
            failures.append(f"stage/i18n contains .ts (runtime asset leaked): {ts_files}")
        for locale in ("zh_CN", "en_US", "ja_JP", "zh_TW"):
            if not (stage / f"bf_app_{locale}.qm").exists():
                failures.append(f"stage/i18n missing bf_app_{locale}.qm")

    if failures:
        return CheckResult(name, False, "; ".join(failures))
    return CheckResult(name, True, ".qm-only staging; qtbase_zh_CN/ja_JP forced; no .ts runtime")


def check_vc_runtime_packaging() -> CheckResult:
    """Ensure a compatible VC++ runtime is bundled and installed before app launch."""
    name = "Windows VC++ runtime prerequisite"
    build_path = REPO_ROOT / "scripts" / "windows" / "build_release.ps1"
    verify_path = REPO_ROOT / "scripts" / "windows" / "verify_windows_release.ps1"
    iss_path = REPO_ROOT / "installer" / "BoundFreeEEG.iss"
    missing = [p.name for p in (build_path, verify_path, iss_path) if not p.is_file()]
    if missing:
        return CheckResult(name, False, f"missing packaging files: {missing}")

    build = _read_text(build_path)
    verify = _read_text(verify_path)
    iss = _read_text(iss_path)
    failures: list[str] = []
    if "VCINSTALLDIR" not in build or "vswhere.exe" not in build:
        failures.append("build must resolve Visual Studio and set VCINSTALLDIR for windeployqt")
    if '"vc_redist.x64.exe"' not in build or "windeployqt did not stage vc_redist.x64.exe" not in build:
        failures.append("build must fail if windeployqt did not stage vc_redist.x64.exe")
    if "BoundFreeVCRedistVersion=$VcRedistVersion" not in build:
        failures.append("build must pass the staged redistributable version to ISCC")
    if '"vc_redist.x64.exe"' not in verify:
        failures.append("Windows acceptance must require the staged VC++ redistributable")
    if "Flags: dontcopy noencryption" not in iss or "ExtractTemporaryFile('vc_redist.x64.exe')" not in iss:
        failures.append("installer must embed the VC++ redistributable as a temporary prerequisite")
    if "function PrepareToInstall(var NeedsRestart: Boolean): String" not in iss or "Exec(RedistPath" not in iss:
        failures.append("installer must run the prerequisite before app installation and check its result")
    if 'Excludes: "vc_redist.x64.exe"' not in iss:
        failures.append("installer must keep the VC++ installer out of the installed application directory")
    if "RegQueryDWordValue(HKLM64" not in iss or "RegQueryStringValue(HKLM64" not in iss:
        failures.append("installer must inspect the 64-bit installed runtime and version")

    if failures:
        return CheckResult(name, False, "; ".join(failures))
    return CheckResult(name, True, "windeployqt redistributable staged, version-bound, and installed when required")

# ---------------------------------------------------------------------------
# Stable/rc signing policy (parse build_release.ps1)
# ---------------------------------------------------------------------------

def check_signing_policy(channel: str) -> CheckResult:
    """Validate build_release.ps1 signing semantics for the given channel."""
    name = f"build_release.ps1 signing policy (channel={channel})"
    path = REPO_ROOT / "scripts" / "windows" / "build_release.ps1"
    if not path.exists():
        return CheckResult(name, False, "build_release.ps1 missing")
    src = _read_text(path)
    failures: list[str] = []

    # 1. Find the channel set that requires signing.
    #    PowerShell form: $ChannelRequiresSigning = $ReleaseChannel -in @("stable", "rc")
    chan_match = re.search(r'ReleaseChannel\s+-in\s+@\(([^)]*)\)', src)
    if not chan_match:
        return CheckResult(name, False, "could not find '-in @(...)' channel set")
    required_channels = [c.strip().strip('"').strip("'") for c in chan_match.group(1).split(",")]
    required = channel in required_channels

    # 2. SkipSigning must be rejected for stable/rc.
    skip_guard = re.search(r"if\s*\(\s*\$SkipSigning\s+-and\s*\$ChannelRequiresSigning\s*\)\s*\{([^}]*)\}", src, re.DOTALL)
    if not skip_guard:
        failures.append("no SkipSigning+ChannelRequiresSigning guard block")
    elif "throw" not in skip_guard.group(1):
        failures.append("SkipSigning guard does not throw")

    # 3. Signing ORDER: stage binaries must be signed BEFORE ISCC, and the
    #    installer signed AFTER ISCC. Use the step-marker Write-Step calls
    #    (6/8, 7/8, 8/8) as anchors so doc-comment mentions don't mislead.
    sign_stage_pos = src.find('Write-Step "6/8')
    iscc_pos = src.find('Write-Step "7/8')
    sign_installer_pos = src.find('Write-Step "8/8')
    stage_req = src.find("required for channel=", sign_stage_pos) if sign_stage_pos >= 0 else -1
    installer_req = src.find("required for channel=", sign_installer_pos) if sign_installer_pos >= 0 else -1
    if sign_stage_pos < 0 or iscc_pos < 0 or sign_installer_pos < 0:
        failures.append("missing sign-stage / ISCC / sign-installer Write-Step markers")
    elif not (sign_stage_pos < iscc_pos < sign_installer_pos):
        failures.append("signing order must be: sign stage -> ISCC -> sign installer")
    if required:
        # For stable/rc, the stage sign step must throw when no cert.
        if "requires code signing but no" not in src:
            failures.append("required channel lacks cert-missing throw")
        if stage_req < 0 or installer_req < 0:
            failures.append("required channel sign blocks not found")

    if failures:
        return CheckResult(name, False, "; ".join(failures))
    return CheckResult(name, True, f"signing_required={required}; stage->ISCC->installer order enforced")


# ---------------------------------------------------------------------------
# Public API for tests
# ---------------------------------------------------------------------------

CHECKS = (
    check_version_json,
    check_version_sync,
    check_sidecar_spec,
    check_iss,
    check_branding_assets,
    check_core_license,
    check_legal,
    check_no_private_keys,
    check_i18n_release,
    check_vc_runtime_packaging,
)


def run_all() -> int:
    results = [c() for c in CHECKS]
    version = json.loads((REPO_ROOT / "version.json").read_text(encoding="utf-8"))
    results.append(check_signing_policy(version["release_channel"]))
    for r in results:
        print(r.format())
    failed = [r for r in results if not r.ok]
    print()
    if failed:
        print(f"FAIL: {len(failed)} check(s) failed")
        return 1
    print(f"PASS: {len(results)} check(s) passed")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate BoundFree EEG release assets.")
    parser.parse_args(argv)
    return run_all()


if __name__ == "__main__":
    sys.exit(main())
