import json
import os
import subprocess
from pathlib import Path

import pytest


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


def test_bf_app_smoke_sidecar_handshake():
    result = _run([str(APP), "--smoke-sidecar"])

    assert result.stdout.strip() == "smoke-sidecar-ok product_version=0.1.0-dev ipc_protocol_version=1"


def test_bf_app_smoke_sidecar_open_missing_file_reports_file_error(tmp_path):
    missing = tmp_path / "missing.edf"

    result = _run([str(APP), "--smoke-sidecar-open", str(missing)])

    assert result.stdout.strip() == f"smoke-sidecar-open-error code=file.not_found path={missing}"


def test_bf_app_smoke_machine_id_from_fixed_inputs():
    result = _run(
        [
            str(APP),
            "--smoke-machine-id",
            "--machine-guid",
            "GUID-001",
            "--mac",
            "AA:BB:CC:DD:EE:FF",
        ]
    )

    assert result.stdout.strip() == "smoke-machine-id 53C42CBF-B2AE90CE-65593CDA-87F80560"


def test_bf_app_smoke_open_generated_csv_fixture():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run([str(APP), "--smoke-open-file", str(sample)])

    assert result.stdout.strip() == (
        "smoke-open-file-ok format=CSV fs=100 n_channels=2 "
        "n_samples=10 duration=0.1 channels=ch1,ch2"
    )


def test_bf_app_smoke_open_slice_generated_csv_fixture():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run([str(APP), "--smoke-open-slice", str(sample), "0.02", "0.05"])

    assert result.stdout.strip() == "smoke-open-slice-ok shape=2x3 first=2 last=14 unit=uV"


def test_bf_app_smoke_ui_open_generated_csv_fixture():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui-open-file", str(sample), "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-open-file-ok format=CSV n_channels=2 n_samples=10" in result.stdout


def test_bf_app_smoke_ui_open_missing_file_reports_failure(tmp_path):
    missing = tmp_path / "missing.csv"

    result = subprocess.run(
        [str(APP), "--smoke-ui-open-file", str(missing), "--language", "en_US"],
        cwd=ROOT,
        env={**os.environ, "QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=6,
    )

    assert result.returncode == 1
    assert "smoke-ui-open-file-failed code=file.not_found" in result.stderr


def test_bf_app_smoke_ui_open_dispose_controller_does_not_crash():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui-open-file-dispose", str(sample), "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert result.stdout.strip() == "smoke-ui-open-file-dispose-ok"


def test_bf_app_smoke_ui_file_open_status_survives_retranslate():
    result = _run(
        [str(APP), "--smoke-ui-file-open-status", "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "loading_en=Opening demo.csv" in result.stdout
    assert "loading_pseudo=[!! Opeeniing demo.csv !!]" in result.stdout
    assert "opened_pseudo=[!! CSV | 100 Hz | 2 chaanneels !!]" in result.stdout
    assert "opened_en=CSV | 100 Hz | 2 channels" in result.stdout


def test_bf_app_smoke_waveform_render_offscreen():
    result = _run(
        [str(APP), "--smoke-waveform-render"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert result.stdout.strip().startswith("smoke-waveform-render-ok image=640x320 non_background_pixels=")


@pytest.mark.parametrize(
    ("payload", "reason_fragment"),
    [
        (
            {
                "handle": "demo",
                "t0": 0.0,
                "t1": 0.03,
                "fs": 100.0,
                "shape": [2, 3, 99],
                "unit": "uV",
                "data": [[1.0, 2.0, 3.0], [11.0, 12.0, 13.0]],
            },
            "expected exactly 2 integers",
        ),
        (
            {
                "handle": "demo",
                "t0": 0.0,
                "t1": 0.03,
                "fs": 100.0,
                "shape": [2],
                "unit": "uV",
                "data": [[1.0, 2.0, 3.0], [11.0, 12.0, 13.0]],
            },
            "expected exactly 2 integers",
        ),
        (
            {
                "handle": "demo",
                "t0": 0.0,
                "t1": 0.03,
                "fs": 100.0,
                "shape": [2, 3],
                "unit": "uV",
                "data": [[1.0, 2.0, 3.0]],
            },
            "expected 2 rows, got 1",
        ),
        (
            {
                "handle": "demo",
                "t0": 0.0,
                "t1": 0.03,
                "fs": 100.0,
                "shape": [2, 3],
                "unit": "uV",
                "data": [[1.0, 2.0], [11.0, 12.0, 13.0]],
            },
            "row 0 expected 3 samples, got 2",
        ),
    ],
)
def test_bf_app_smoke_parse_slice_payload_rejects_shape_data_mismatch(tmp_path, payload, reason_fragment):
    payload_path = tmp_path / "slice.json"
    payload_path.write_text(json.dumps(payload), encoding="utf-8")

    result = subprocess.run(
        [str(APP), "--smoke-parse-slice-payload", str(payload_path)],
        cwd=ROOT,
        env={**os.environ, "PYTHONPATH": str(ROOT / "sidecar")},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=2,
    )

    assert result.returncode == 1
    assert result.stdout.strip() == (
        f"smoke-parse-slice-payload-error code=protocol.invalid_params field=shape reason={reason_fragment}"
    )


@pytest.mark.parametrize("bad_value", ["bad", None, True])
def test_bf_app_smoke_parse_slice_payload_rejects_non_numeric_samples(tmp_path, bad_value):
    payload = {
        "handle": "demo",
        "t0": 0.0,
        "t1": 0.03,
        "fs": 100.0,
        "shape": [2, 3],
        "unit": "uV",
        "data": [[1.0, bad_value, 3.0], [11.0, 12.0, 13.0]],
    }
    payload_path = tmp_path / "slice.json"
    payload_path.write_text(json.dumps(payload), encoding="utf-8")

    result = subprocess.run(
        [str(APP), "--smoke-parse-slice-payload", str(payload_path)],
        cwd=ROOT,
        env={**os.environ, "PYTHONPATH": str(ROOT / "sidecar")},
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=2,
    )

    assert result.returncode == 1
    assert result.stdout.strip().startswith(
        "smoke-parse-slice-payload-error code=protocol.invalid_params field=data reason="
    )


def test_bf_app_smoke_minmax_decimator():
    result = _run([str(APP), "--smoke-minmax-decimator"])

    assert result.stdout.strip() == "smoke-minmax-decimator 0:0,1 1:2,3 2:4,5"


def test_bf_app_smoke_request_generation_latest_wins():
    result = _run([str(APP), "--smoke-request-generation"])

    assert result.stdout.strip() == (
        "smoke-request-generation-ok first=1 second=2 "
        "first_current=false second_current=true after_invalidate=false"
    )


def test_bf_app_smoke_waveform_view_state_clamps_and_toggles():
    result = _run([str(APP), "--smoke-waveform-view-state"])

    assert result.stdout.strip() == (
        "smoke-waveform-view-state-ok window=120 amplitude=50 "
        "first_channel=3 visible=0,2,3"
    )
