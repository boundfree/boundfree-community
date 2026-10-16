"""Exercise the persistent worker through a countable framed IPC peer."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest


def test_file_session_reuse_coalescing_recovery_and_destruction(tmp_path):
    root = Path(__file__).resolve().parents[2]
    candidates = [root / "native" / build / config / name
                  for build in ("build-windows", "build")
                  for config in ("Release", "Debug", "")
                  for name in ("bf_app.exe", "bf_app")]
    app = next((candidate for candidate in candidates if candidate.is_file()), None)
    if app is None:
        pytest.skip("请先构建 bf_app")
    sidecar = tmp_path / "sidecar"
    sidecar.mkdir()
    shutil.copyfile(Path(__file__).with_name("file_session_fake_sidecar.py"),
                    sidecar / "boundfree_sidecar.py")
    env = os.environ.copy()
    env.update(BF_PYTHON_EXECUTABLE=sys.executable, BF_SIDECAR_EXECUTABLE="",
               QT_QPA_PLATFORM="offscreen")
    result = subprocess.run([str(app), "--smoke-file-session-reuse", str(tmp_path)],
                            cwd=root, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "smoke-file-session-reuse-ok" in result.stdout
    records = [json.loads(line) for line in (tmp_path / "requests.jsonl").read_text().splitlines()]
    hellos = [row for row in records if row["method"] == "hello"]
    # One initial process, one after an open error, one after a crashed peer.
    assert len(hellos) == 3, records
    first_pid = hellos[0]["pid"]
    initial = [row for row in records if row["pid"] == first_pid]
    opens = [row["params"]["path"] for row in initial if row["method"] == "open"]
    assert opens == ["a.csv", "a.csv", "b.csv", "bad.csv"], initial
    first_switch = next(i for i, row in enumerate(initial)
                        if row["method"] == "open" and row["params"]["path"] == "b.csv")
    assert initial[first_switch - 1]["method"] == "close", initial
    assert initial[first_switch - 1]["params"]["handle"] == "a.csv", initial
    slices = [row["params"]["t0"] for row in records if row["method"] == "get_slice"]
    assert slices == [0, 1, 2, 0, 0, 10, 13, 0, 30, 1, 20], records
    assert records[-2]["method"] == "close", records
    assert records[-2]["params"]["handle"] == "a.csv", records
    assert records[-1]["method"] == "eof", records
    assert not any(row["method"] == "get_slice" and row["params"]["t0"] == 21
                   for row in records), records
