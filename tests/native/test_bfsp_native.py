"""Exercise native BFSP invariants through the normal cross-platform test suite."""
import os
from pathlib import Path
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]


def native_binary(name):
    explicit = os.environ.get("BFSP_NATIVE_BUILD_DIR")
    builds = [Path(explicit)] if explicit else [ROOT / "native" / "build", ROOT / "native" / "build-windows"]
    filename = name + (".exe" if os.name == "nt" else "")
    for build in builds:
        for config in ("", "Release", "Debug"):
            candidate = build / config / filename
            if candidate.is_file():
                return candidate
    if explicit:
        pytest.fail(f"Required BFSP test binary {filename} missing under {explicit}")
    pytest.skip(f"Build native target {name} to run this regression")


@pytest.mark.parametrize("name", ["signal_model_test", "acquisition_simulator_test", "clock_mapping_test",
                                 "acquisition_format_test", "bfsp_protocol_test", "signal_window_test", "live_ica_projection_test"])
def test_bfsp_native_invariants(name):
    args = [str(native_binary(name))]
    if name == "bfsp_protocol_test":
        args.append(str(ROOT))
    result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, f"{name}\n{result.stdout}\n{result.stderr}"
    assert "PASS" in result.stdout + result.stderr, result.stdout + result.stderr


@pytest.mark.parametrize("case", ["--identity", "--multiple", "--stale-ack", "--reuse-stream", "--lose-start",
                                 "--delay-start", "--early-stop", "--no-data", "--no-ack", "--gap", "--bad-crc",
                                 "--recovery-pending", "--recovery-deadline", "--recovery-late-response",
                                 "--recovery-late-conflict", "--recovery-status", "--parser-batch-fault",
                                 "--parser-batch-reopen", "--slow-block", "--long-block", "--large-block-period",
                                 "--recovered-start-no-data"])
def test_bfsp_tcp_interoperability(case):
    env = os.environ.copy()
    env.setdefault("BF_PYTHON_EXECUTABLE", sys.executable)
    env["BF_REPO_ROOT"] = str(ROOT)
    result = subprocess.run([str(native_binary("bfsp_tcp_client_test")), case],
                            cwd=ROOT, env=env, capture_output=True, text=True, timeout=45)
    assert result.returncode == 0, f"{case}\n{result.stdout}\n{result.stderr}"
    assert "BFSP_TCP_CLIENT_TEST PASS" in result.stdout, result.stdout


def windows_qt_environment(env):
    """Windows offscreen runs need the system font directory, and Qt logs to stderr only when forced."""
    if os.name == "nt":
        env.setdefault("QT_QPA_FONTDIR", str(Path(os.environ["SystemRoot"]) / "Fonts"))
        env.setdefault("QT_FORCE_STDERR_LOGGING", "1")
    return env


def run_process_tree(command, env, timeout):
    """Run a test binary; on timeout kill it with any simulator children and report its output.

    subprocess.run(timeout=...) only kills the direct child. On Windows a surviving
    simulator keeps the output pipes open, so collecting output would block indefinitely.
    """
    flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               text=True, encoding="utf-8", errors="replace", creationflags=flags,
                               start_new_session=os.name != "nt")
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        if os.name == "nt":
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True)
        else:
            os.killpg(process.pid, 9)
        stdout, stderr = process.communicate(timeout=30)
        pytest.fail(f"{command[0]} timed out after {timeout} s\n{stdout}\n{stderr}")
    return process.returncode, stdout, stderr


@pytest.mark.parametrize("name", ["acquisition_tcp_source_test", "acquisition_ui_test", "capture_recorder_test", "acquisition_diagnostic_log_test"])
def test_acquisition_raw_recording(name):
    env = os.environ.copy()
    env.setdefault("BF_PYTHON_EXECUTABLE", sys.executable)
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    windows_qt_environment(env)
    returncode, stdout, stderr = run_process_tree([str(native_binary(name)), str(ROOT)], env, timeout=240)
    assert returncode == 0, f"{stdout}\n{stderr}"
    assert "PASS" in stdout + stderr, stdout + stderr


def test_unified_device_waveform():
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("BF_PYTHON_EXECUTABLE", sys.executable)
    windows_qt_environment(env)
    result = subprocess.run([str(native_binary("bf_app")), "--smoke-ui-device-waveform"],
                            cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, f"{result.stdout}\n{result.stderr}"
    assert "smoke-ui-device-waveform-ok" in result.stdout, result.stdout


def test_common_signal_processing_analysis_and_independent_raw_recording():
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("BF_PYTHON_EXECUTABLE", sys.executable)
    env["BF_SIDECAR_EXECUTABLE"] = ""
    windows_qt_environment(env)
    result = subprocess.run([str(native_binary("bf_app")), "--smoke-ui-unified-tools"],
                            cwd=ROOT, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=180)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "UNIFIED_SIGNAL_TOOLS PASS" in result.stdout, result.stdout


def test_continuous_device_ica_with_fixed_reviewed_model():
    env = os.environ.copy()
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("BF_PYTHON_EXECUTABLE", sys.executable)
    env["BF_SIDECAR_EXECUTABLE"] = ""
    windows_qt_environment(env)
    result = subprocess.run([str(native_binary("bf_app")), "--smoke-ui-live-ica"],
                            cwd=ROOT, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=180)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "LIVE_ICA_UI PASS" in result.stdout, result.stdout
