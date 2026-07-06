import json
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = ROOT / "native" / "build"
APP = BUILD_DIR / "bf_app"


def _run(args, env_overrides=None):
    env = os.environ.copy()
    env.setdefault("PYTHONPATH", str(ROOT / "sidecar"))
    if env_overrides:
        env.update(env_overrides)
    return subprocess.run(
        args,
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )


def test_bf_app_version_matches_version_json():
    version = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))

    result = _run([str(APP), "--version"])

    assert result.stdout.strip() == f'{version["product_name"]} {version["version"]}'


def test_bf_app_smoke_ui_starts_offscreen():
    result = _run(
        [str(APP), "--smoke-ui", "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-ok" in result.stdout
