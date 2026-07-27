"""Static contract tests for Windows dev configuration and WSL sync script.

Verifies:
1. config/windows-dev.example.json exists, is parseable JSON, contains all schema fields,
   and contains no sensitive private key or PFX fields.
2. config/windows-dev.local.json is ignored by .gitignore.
3. scripts/sync_to_windows.sh reads config/windows-dev.local.json and wsl_sync_target,
   while allowing explicit positional parameter override.
"""

import json
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
EXAMPLE_CONFIG = ROOT / "config" / "windows-dev.example.json"
GITIGNORE = ROOT / ".gitignore"
SYNC_SCRIPT = ROOT / "scripts" / "wsl" / "sync_to_windows.sh"


def _text(p: Path) -> str:
    return p.read_text(encoding="utf-8")


class TestWindowsDevConfig:
    def test_example_config_exists_and_parseable(self):
        """config/windows-dev.example.json must exist and be valid JSON."""
        assert EXAMPLE_CONFIG.exists(), f"Missing example config file: {EXAMPLE_CONFIG}"
        data = json.loads(_text(EXAMPLE_CONFIG))
        assert isinstance(data, dict), "Example config root must be a JSON object"

    def test_example_config_schema_fields(self):
        """Example config must include all required specification fields."""
        assert EXAMPLE_CONFIG.exists(), f"Missing example config file: {EXAMPLE_CONFIG}"
        data = json.loads(_text(EXAMPLE_CONFIG))
        required_fields = {
            "schema_version",
            "wsl_sync_target",
            "qt_root",
            "bootstrap_python",
            "bootstrap_python_args",
            "venv_dir",
            "iscc_path",
            "license_public_key_file",
            "configuration",
        }
        missing = required_fields - set(data.keys())
        assert not missing, f"config/windows-dev.example.json missing fields: {missing}"

    def test_example_config_no_private_keys_or_pfx(self):
        """Example config must not expose private key paths/contents or PFX certificates."""
        assert EXAMPLE_CONFIG.exists(), f"Missing example config file: {EXAMPLE_CONFIG}"
        t = _text(EXAMPLE_CONFIG)
        data = json.loads(t)
        forbidden_terms = ["private", "pfx", "signing_cert", "pem_private"]
        for key in data.keys():
            lower_key = key.lower()
            for term in forbidden_terms:
                assert term not in lower_key, f"Forbidden sensitive key '{key}' found in example config"

        for key, val in data.items():
            if isinstance(val, str):
                lower_val = val.lower()
                assert ".pfx" not in lower_val, f"PFX certificate file referenced in '{key}': '{val}'"
                assert "private" not in lower_val, f"Private key referenced in '{key}': '{val}'"

    def test_local_config_is_gitignored(self):
        """config/windows-dev.local.json must be ignored in .gitignore."""
        t = _text(GITIGNORE)
        assert re.search(r"windows-dev\.local\.json|\*\.local\.json|config/\*\.local\.json", t), (
            ".gitignore must ignore config/windows-dev.local.json"
        )


class TestWslSyncScript:
    def test_sync_script_reads_local_config_and_target(self):
        """scripts/sync_to_windows.sh must read config/windows-dev.local.json and wsl_sync_target."""
        t = _text(SYNC_SCRIPT)
        assert "windows-dev.local.json" in t, "sync_to_windows.sh must check/read config/windows-dev.local.json"
        assert "wsl_sync_target" in t, "sync_to_windows.sh must parse wsl_sync_target field"

    def test_sync_script_explicit_argument_overrides_config(self):
        """Explicit positional parameter ($1) must override target read from local config."""
        t = _text(SYNC_SCRIPT)
        # Position parameter $1 must be evaluated before falling back to config file or target_root default
        assert re.search(
            r'target_root=.*\$1|if\s*\[\s*-n\s+"\$\{1:-\}"\s*\]|if\s*\[\s*-n\s+"\$1"\s*\]|for\s+arg\s+in\s+"\$@"',
            t,
        ) or (
            "windows-dev.local.json" in t and ("$1" in t or "${1" in t or "$arg" in t)
        ), "sync_to_windows.sh must prioritize explicit positional argument over local JSON config target"
    def test_sync_script_supports_init_flag(self):
        """scripts/wsl/sync_to_windows.sh must support --init to initialize empty directories."""
        t = _text(SYNC_SCRIPT)
        assert "--init" in t, "sync_to_windows.sh must support --init option"
        assert "init_mode" in t, "sync_to_windows.sh must parse init_mode"
