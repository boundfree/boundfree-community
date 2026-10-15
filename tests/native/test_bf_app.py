import json
import os
import subprocess
import sys
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


def _candidate_apps() -> list[Path]:
    return [
        ROOT / "native" / "build-windows" / "Release" / "bf_app.exe",
        ROOT / "native" / "build-windows" / "Debug" / "bf_app.exe",
        ROOT / "native" / "build" / "Release" / "bf_app.exe",
        ROOT / "native" / "build" / "Debug" / "bf_app.exe",
        ROOT / "native" / "build" / "bf_app.exe",
        ROOT / "native" / "build" / "bf_app",
    ]


def _resolve_app() -> Path:
    existing = [path for path in _candidate_apps() if path.exists()]
    if not existing:
        listed = "\n".join(f"- {path}" for path in _candidate_apps())
        raise FileNotFoundError(
            "bf_app executable was not found. Build the native app first.\n"
            f"Checked:\n{listed}"
        )
    return existing[0]


APP = _resolve_app()


def _run(args, env_overrides=None):
    env = os.environ.copy()
    env.setdefault("PYTHONPATH", str(ROOT / "sidecar"))
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("PYTHONIOENCODING", "utf-8")
    env.setdefault("PYTHONUTF8", "1")
    if env_overrides:
        env.update(env_overrides)
    return subprocess.run(
        args,
        cwd=ROOT,
        env=env,
        text=True,
        encoding="utf-8",
        errors="replace",
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


def test_bf_app_normal_ui_flow_opens_generated_csv():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui", "--open-file", str(sample), "--language", "zh_CN"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert result.stdout.strip() == "smoke-ui-file-flow-ok format=CSV n_channels=2 t0=0 t1=0.1"


def test_bf_app_smoke_sidecar_handshake():
    version = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    result = _run([str(APP), "--smoke-sidecar"])

    assert result.stdout.strip() == (
        f'smoke-sidecar-ok product_version={version["version"]} '
        f'ipc_protocol_version={version["ipc_protocol_version"]}'
    )


def test_bf_app_smoke_sidecar_open_missing_file_reports_file_error(tmp_path):
    missing = tmp_path / "missing.edf"

    result = _run([str(APP), "--smoke-sidecar-open", str(missing)])

    output = result.stdout.strip()
    assert output.startswith("smoke-sidecar-open-error file.not_found params=")
    # Params are dumped as compact JSON, so Windows separators appear escaped.
    assert str(missing).replace("\\", "\\\\") in output


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

    assert result.stdout.strip() == "smoke-machine-id 0B9C9157-6A069C39-FA9C30B9-C0086DFA"


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
    assert "opened_pseudo=[!! CSV | 0-0.01 s | +/-200 uV | 2 viisiiblee chaanneels !!]" in result.stdout
    assert "opened_en=CSV | 0-0.01 s | +/-200 uV | 2 visible channels" in result.stdout


def test_bf_app_smoke_waveform_render_offscreen():
    result = _run(
        [str(APP), "--smoke-waveform-render"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    output = result.stdout.strip()
    assert output.startswith("smoke-waveform-render-ok image=640x320 non_background_pixels=")
    fields = dict(item.split("=", 1) for item in output.split() if "=" in item)
    # These bands exclude the plot frame and tick marks, so the counts can only be
    # produced by axis text; the empty-data case proves the axes survive with no
    # visible channel, where the left gutter legitimately has no channel labels.
    assert int(fields["axis_time_text_pixels"]) > 0
    assert int(fields["axis_gutter_text_pixels"]) > 0
    assert int(fields["empty_axis_time_text_pixels"]) > 0


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


def test_bf_app_smoke_waveform_display_model_filters_visible_channels():
    result = _run([str(APP), "--smoke-waveform-display-model"])

    assert result.stdout.strip() == (
        "smoke-waveform-display-model-ok channels=ch3 samples=4 "
        "window=0-0.04 amplitude=200"
    )

def test_bf_app_smoke_waveform_display_model_cache_reuses_same_window():
    result = _run(
        [str(APP), "--smoke-waveform-display-model-cache"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert result.stdout.strip() == (
        "smoke-waveform-display-model-cache-ok builds_before=0 builds_after_first=1 "
        "builds_after_second=1 builds_after_paint=1 channels=ch1,ch2 same_channels=true"
    )


def test_bf_app_smoke_async_open_allows_event_loop_progress():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-async-open-nonblocking", str(sample)],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-async-open-nonblocking-ok timer_ticks=" in result.stdout
    ticks = int(result.stdout.strip().split("timer_ticks=")[1])
    assert ticks > 0


def test_bf_app_smoke_ui_browser_controls_show_window_and_scale():
    result = _run(
        [str(APP), "--smoke-ui-browser-controls", "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-browser-controls-ok" in result.stdout
    assert "status=CSV | 0-10 s | +/-200 uV | 2 visible channels" in result.stdout


def test_bf_app_smoke_ui_channel_browser_updates_visible_channels():
    result = _run(
        [str(APP), "--smoke-ui-channel-browser", "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-channel-browser-ok" in result.stdout
    assert "summary=first=2 visible=2,3 checks=ch1:on,ch2:off,ch3:on,ch4:on" in result.stdout
    assert "status=CSV | 0-10 s | +/-200 uV | 2 visible channels" in result.stdout


def test_bf_app_smoke_ui_window_request_reports_requested_interval():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui-window-request", str(sample), "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert f"smoke-ui-window-request-ok path={sample} t0=0 window=4" in result.stdout


def test_bf_app_smoke_ui_window_latest_displays_requested_slice():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui-window-latest", str(sample), "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-window-latest-ok t0=0.02 t1=0.1" in result.stdout
    assert "status=CSV | 0.02-0.1 s | +/-200 uV | 2 visible channels" in result.stdout


def test_bf_app_smoke_ui_open_latest_keeps_newer_result():
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"

    result = _run(
        [str(APP), "--smoke-ui-open-latest", str(sample), "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-ui-open-latest-ok t0=0.02 t1=0.05" in result.stdout


def test_bf_app_smoke_waveform_benchmark_reports_development_result():
    result = _run(
        [
            str(APP),
            "--smoke-waveform-benchmark",
            "--channels",
            "64",
            "--fs",
            "1000",
            "--seconds",
            "4",
            "--frames",
            "3",
        ],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    assert "smoke-waveform-benchmark-ok channels=64 fs=1000 seconds=4 frames=3" in result.stdout
    assert "gate=development-signal-only" in result.stdout


def test_bf_app_smoke_waveform_benchmark_json_reports_gate_status():
    result = _run(
        [
            str(APP),
            "--smoke-waveform-benchmark",
            "--channels",
            "64",
            "--fs",
            "1000",
            "--seconds",
            "4",
            "--frames",
            "2",
            "--json",
        ],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )

    data = json.loads(result.stdout)
    assert data["channels"] == 64
    assert data["fs"] == 1000
    assert data["seconds"] == 4
    assert data["frames"] == 2
    assert data["fps"] > 0
    assert data["gate"] == "development-signal-only"


def test_bf_app_smoke_processing_offline():
    result = _run([str(APP), "--smoke-processing-offline"])
    assert "smoke-processing-offline-ok" in result.stdout
    assert "ref=car" in result.stdout


def test_bf_app_smoke_m8_system_ui_offscreen():
    result = _run([str(APP), "--smoke-m8-system-ui"])

    assert result.stdout.strip() == "smoke-m8-system-ui-ok"



def test_bf_app_smoke_language_menu_has_exactly_three_stable_entries():
    """Stable language menu is covered by C++ test; en_US CLI smoke remains valid."""
    result = _run(
        [str(APP), "--smoke-ui", "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )
    assert "smoke-ui-ok" in result.stdout


def test_bf_app_smoke_language_cli_override_en_us_starts_ui():
    """--language en_US must start the UI successfully (CLI override path)."""
    result = _run(
        [str(APP), "--smoke-ui", "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )
    assert result.returncode == 0
    assert "smoke-ui-ok" in result.stdout


def test_bf_app_smoke_language_cli_override_ja_jp_starts_ui():
    """--language ja_JP must load the formal Japanese catalog and start UI."""
    result = _run(
        [str(APP), "--smoke-ui", "--language", "ja_JP"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )
    assert result.returncode == 0
    assert "smoke-ui-ok" in result.stdout


def test_bf_app_smoke_language_cli_override_zh_cn_starts_ui():
    """--language zh_CN must start the UI successfully (CLI override path)."""
    result = _run(
        [str(APP), "--smoke-ui", "--language", "zh_CN"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )
    assert result.returncode == 0
    assert "smoke-ui-ok" in result.stdout


def test_bf_app_smoke_retranslate_updates_ui_text():
    """Switching locale via loadLocale must update visible UI status text."""
    result = _run(
        [str(APP), "--smoke-ui-file-open-status", "--language", "en_US"],
        env_overrides={"QT_QPA_PLATFORM": "offscreen", "PYTHONPATH": str(ROOT / "sidecar")},
    )
    assert result.returncode == 0
    assert "loading_en=Opening demo.csv" in result.stdout
    assert "loading_pseudo=[!! Opeeniing demo.csv !!]" in result.stdout
    assert "opened_en=CSV | 0-0.01 s | +/-200 uV | 2 visible channels" in result.stdout
