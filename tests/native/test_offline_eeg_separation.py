"""Run real offline EEG/EOG native regressions in the regular pytest suite."""

import os
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]


def executable(name: str) -> Path:
    if os.name == "nt":
        candidates = [ROOT / "native" / directory / config / (name + ".exe")
                      for directory in ("build-windows", "build") for config in ("Release", "Debug", "")]
    else:
        candidates = [ROOT / "native" / "build" / name]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    pytest.skip(f"Native test executable {name} is not built; build the matching CMake target first")


def analysis_environment(*modules: str) -> dict[str, str]:
    env = os.environ.copy()
    python = env.setdefault("BF_PYTHON_EXECUTABLE", sys.executable)
    env.setdefault("PYTHONIOENCODING", "utf-8")
    env.setdefault("PYTHONUTF8", "1")
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    if os.name == "nt":
        # The offscreen platform has no font directory on Windows; without it text renders no glyphs.
        env.setdefault("QT_QPA_FONTDIR", str(Path(os.environ["SystemRoot"]) / "Fonts"))
        env.setdefault("QT_FORCE_STDERR_LOGGING", "1")
    if modules:
        result = subprocess.run([python, "-c", "; ".join("import " + module for module in modules)],
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
        if result.returncode != 0 and "ModuleNotFoundError" in result.stderr:
            pytest.skip("Optional analysis dependencies unavailable in native sidecar Python: " + result.stderr.strip())
        assert result.returncode == 0, result.stderr
    return env


def run(binary: Path, args: list[str], env: dict[str, str], timeout: int = 240,
        expected_marker: str | None = None) -> None:
    result = subprocess.run([str(binary), *args], cwd=ROOT, env=env, capture_output=True,
                            text=True, encoding="utf-8", errors="replace", timeout=timeout)
    assert result.returncode == 0, f"{binary.name}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    if expected_marker:
        assert expected_marker in result.stdout, result.stdout
    else:
        assert "PASS" in result.stdout or "eeg-separation-ui-ok" in result.stdout, result.stdout


@pytest.mark.parametrize("name", ["ica_core_test", "eeg_separation_test"])
def test_offline_separation_numeric_regressions(name):
    run(executable(name), [], analysis_environment())


def test_offline_separation_real_sidecar_controller(tmp_path):
    binary = executable("eeg_separation_controller_test")
    env = analysis_environment("numpy", "h5py", "neo", "pyedflib")
    run(binary, [str(ROOT), str(tmp_path), str(ROOT / "tests" / "fixtures" / "generated" / "offline-eeg")], env)


def test_offline_separation_workspace_interaction():
    binary = executable("eeg_separation_workspace_test")
    run(binary, [], analysis_environment("numpy", "h5py"))


def test_offline_separation_persistent_workspace_navigation():
    run(executable("bf_app"), ["--smoke-ui-workspace-navigation"],
        analysis_environment("numpy", "h5py"),
        expected_marker="smoke-ui-workspace-navigation-ok")
