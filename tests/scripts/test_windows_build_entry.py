"""Static contract tests for scripts/windows/build_windows.ps1 — the one-shot Windows dev build entry.

Verifies the observable safety and orchestration contracts: platform gate,
channel allowlist, mandatory SkipSigning, .venv lifecycle, license key handling,
Qt resolution chain, and passthrough to build_release.ps1 / verify_windows_release.ps1.
"""

import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
BUILD_WINDOWS = ROOT / "scripts" / "windows" / "build_windows.ps1"


def _text(p: Path) -> str:
    return p.read_text(encoding="utf-8")


class TestBuildWindowsPs1:
    # -- Platform gate ---------------------------------------------------------

    def test_windows_only_guard(self):
        """Throws immediately when OS is not Windows_NT."""
        t = _text(BUILD_WINDOWS)
        assert '$env:OS -ne "Windows_NT"' in t
        assert re.search(
            r"throw.*Windows.*PowerShell|throw.*Windows.*10|throw.*must run",
            t,
        )

    # -- Channel gate ----------------------------------------------------------

    def test_rejects_rc_and_stable_with_actionable_message(self):
        """rc/stable channels throw because this entry never signs; message
        must direct user to build_release.ps1 with Authenticode."""
        t = _text(BUILD_WINDOWS)
        assert re.search(
            r'release_channel\s+-notin\s+@\(\s*"dev"\s*,\s*"alpha"\s*,\s*"beta"\s*\)',
            t,
        )
        assert re.search(
            r"throw.*build_release\.ps1|throw.*Authenticode|throw.*signed|throw.*bypass",
            t,
        )

    @pytest.mark.parametrize("ch,accepted", [
        ("dev", True),
        ("alpha", True),
        ("beta", True),
        ("rc", False),
        ("stable", False),
    ])
    def test_channel_allowlist(self, ch, accepted):
        """Only dev/alpha/beta pass the channel gate; rc/stable are rejected."""
        t = _text(BUILD_WINDOWS)
        m = re.search(r"release_channel\s+-notin\s+@\(([^)]+)\)", t)
        assert m, "cannot locate channel -notin allowlist"
        allowed = [c.strip().strip("'\"") for c in m.group(1).split(",") if c.strip()]
        assert (ch in allowed) == accepted

    # -- Always SkipSigning ----------------------------------------------------

    def test_always_passes_skip_signing(self):
        """The one-shot entry NEVER signs; -SkipSigning is hard-coded."""
        t = _text(BUILD_WINDOWS)
        assert "-SkipSigning" in t

    # -- .venv lifecycle -------------------------------------------------------

    def test_creates_or_reuses_venv(self):
        """Auto-creates .venv if absent; uses .venv\\Scripts\\python.exe."""
        t = _text(BUILD_WINDOWS)
        assert "venv" in t
        assert ".venv" in t
        assert "Scripts\\python.exe" in t

    def test_installs_sidecar_build_deps(self):
        """Installs sidecar[formats] + pyinstaller via venv pip."""
        t = _text(BUILD_WINDOWS)
        assert "pyinstaller" in t
        # venv pip install with editable sidecar install (sidecar[formats])
        assert "sidecar" in t
        assert "pip" in t
        assert "install" in t

    def test_verifies_imports_after_install(self):
        """After pip install, validates key packages import cleanly."""
        t = _text(BUILD_WINDOWS)
        assert "sidecar-build-dependencies-ok" in t

    def test_uses_license_public_pem_default(self):
        """LicensePublicKeyFile defaults to repo license_public.pem.

        The error message for a missing key also instructs the operator to
        export only the public key — never supply a private key.
        """
        t = _text(BUILD_WINDOWS)
        assert "license_public.pem" in t
        # The throw message for missing key must mention "do not provide a
        # private key" so an operator who accidentally puts a private key
        # there gets an actionable message.
        assert re.search(r"do not provide.*private|not.*private.*key", t, re.IGNORECASE)

    def test_calls_build_release_ps1_with_correct_args(self):
        """Forwards QtRoot, venv Python, LicensePublicKeyFile, and -SkipSigning
        through named parameters or a named hashtable, without positional array forwarding."""
        t = _text(BUILD_WINDOWS)
        assert "build_release.ps1" in t
        assert not re.search(
            r"Invoke-Checked\s+[\"'].*?[\"']\s+\$ReleaseScript", t
        ), "Release script must not be invoked through Invoke-Checked wrapper"
        assert "$buildArgs.ToArray()" not in t, "Release script args must not be forwarded via $buildArgs.ToArray()"
        assert "$buildArgs = " not in t, "Release script must not build an array of arguments for forwarding"

        assert re.search(r"&\s+\$ReleaseScript\b", t), "Release script must be called directly via call operator (&)"
        release_calls = re.findall(r"&\s+\$ReleaseScript\s+[^\n]+", t)
        assert len(release_calls) >= 1, "Must contain at least one direct call to $ReleaseScript"

        required_params = [
            "-QtRoot",
            "-Python",
            "-Configuration",
            "-LicensePublicKeyFile",
            "-SkipSigning",
        ]
        for call in release_calls:
            named_table = re.fullmatch(r"&\s+\$ReleaseScript\s+@(\w+)\s*", call)
            if named_table:
                # Hashtable splatting binds names, unlike the positional arrays
                # rejected above. Verify the actual values, not just key names.
                table_name = re.escape(named_table.group(1))
                definition = re.search(
                    rf"\${table_name}\s*=\s*@\{{(.*?)^\s*\}}",
                    t[:t.index(call)], re.MULTILINE | re.DOTALL,
                )
                assert definition, "Named release parameters must be defined before the call"
                bindings = {
                    "QtRoot": "$ResolvedQtRoot",
                    "Python": "$VenvPython",
                    "Configuration": "$Configuration",
                    "LicensePublicKeyFile": "$LicensePublicKeyFile",
                    "SkipSigning": "$true",
                }
                for name, value in bindings.items():
                    assert re.search(
                        rf"^\s*{name}\s*=\s*{re.escape(value)}\s*$",
                        definition.group(1), re.MULTILINE | re.IGNORECASE,
                    ), f"Named release parameter {name} must forward {value}"
                continue
            for param in required_params:
                assert param in call, f"Direct call to $ReleaseScript missing required parameter {param}"
    # -- Verification ----------------------------------------------------------

    def test_runs_verify_by_default_skippable(self):
        """Calls verify_windows_release.ps1 by default; -SkipVerification
        suppresses via early return."""
        t = _text(BUILD_WINDOWS)
        assert "verify_windows_release.ps1" in t
        assert re.search(r"SkipVerification.*return", t, re.DOTALL)

    def test_skip_verification_warns_before_return(self):
        """-SkipVerification emits a warning so the operator sees it."""
        t = _text(BUILD_WINDOWS)
        assert re.search(r"Write-Warning.*[Vv]erif", t)

    # -- SkipInstaller passthrough ---------------------------------------------

    def test_skip_installer_forwarded_to_both_steps(self):
        """-SkipInstaller forwarded to both build_release.ps1 and
        verify_windows_release.ps1."""
        t = _text(BUILD_WINDOWS)
        assert "SkipInstaller" in t
        assert re.search(r"build_release\.ps1.*SkipInstaller", t, re.DOTALL)
        assert re.search(r"verify_windows_release\.ps1.*SkipInstaller|verifyArgs.*SkipInstaller", t, re.DOTALL)
    # -- QtRoot resolution -----------------------------------------------------

    def test_qtroot_resolution_chain(self):
        """QtRoot: explicit param → BF_QT_ROOT → QT_ROOT → drive scan."""
        t = _text(BUILD_WINDOWS)
        assert "BF_QT_ROOT" in t
        assert "QT_ROOT" in t
        assert re.search(r"Get-PSDrive|FileSystem", t)

    def test_qtroot_not_found_gives_actionable_error(self):
        """When no Qt found, error directs user to install or set env var."""
        t = _text(BUILD_WINDOWS)
        assert re.search(
            r"throw.*Qt.*not found|throw.*msvc2022_64|throw.*BF_QT_ROOT|throw.*Install Qt",
            t,
        )
