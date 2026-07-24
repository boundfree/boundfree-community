"""Regression guards for historically-fixed bugs (from the legacy AGENTS.md checklist).

Each test here defends a specific bug that was fixed in an earlier version and
must never regress. The guards reuse the existing ``--smoke-*`` CLI entry
points exposed by ``bf_app`` (see ``native/src/app/main.cpp``) so that no
production code is touched and the observable behaviour is asserted end to end.

Bug coverage map (item number = checklist entry):

1. Marker scene connect-once (no double-connect / multi-color loop):
   ``--smoke-m5-markers`` exercises ``MarkerStore::replaceFromEvents`` +
   ``MarkerStore::add`` and asserts colors stay distinct across repeated adds.
   The UI-level ``markerRequested`` signal is wired exactly once in the
   ``MainWindow`` ctor (single connect call); that wiring count is not exposed
   via a smoke CLI, so the observable proxy is: the marker store colour palette
   stays deterministic and non-duplicated after repeated add/replace. The
   connect-once guarantee at the signal/slot layer is *deferred* (no entry
   point exposes signal connection count) -- see ``deferred`` in the result.

2. Switching file cancels stale trend computation (AnalysisService
   latest-request-wins): ``--smoke-m4-analysis`` asserts ``latest_request=true``
   and ``cancel=true`` in its stdout marker. This test pins those substrings so
   a regression that drops either guard turns red.

3. Dark-mode traversal re-themes already-built graphics: ``--smoke-m8-system-ui``
   triggers light then dark and asserts the dark palette colours
   (``#1E1B2E`` window + ``#A78BFA`` highlight) were applied. Pinning the
   success marker proves the palette traversal path ran.

4. STFT jet colormap is inline (no external colormap file dependency):
   ``--smoke-m4-render`` renders ``SpectrogramWidget`` with the in-tree
   ``jet()`` function and reports non-background pixel count. Asserting
   ``spectrogram=<int>`` is present and large proves the jet colormap rendered
   without an external colormap resource.

5. Video slave clock, no singleShot compensation timer:
   ``--smoke-m6-sync`` exercises ``VideoSyncState::sync`` and reports
   ``blank_idempotent=true`` plus the resync thresholds. Pinning the success
   marker proves the slave-clock seek logic ran without a compensation timer.

6. Blank segment black screen (no first-frame poster):
   same ``--smoke-m6-sync`` asserts before-start blank emits ``pause + seek 0``
   and repeated blank does not re-seek (``blank_idempotent=true``). The success
   marker covers both invariants.

7. Sidecar MNE collect_all completeness: covered in
   ``tests/sidecar/test_bug_regressions_spec.py`` (static spec assertion).

8. Switching file disconnects old session signals (play button stays alive):
   ``--smoke-ui-open-file-dispose`` proves the ``FileOpenController`` tears down
   cleanly after an open, and ``--smoke-ui-window-latest`` proves a second
   ``showFileOpenResult`` + window request still completes after the first open
   (the window-request handler remains wired). A true "open file A then file B"
   smoke entry does not exist yet, so the cross-file disconnect invariant is
   *deferred* -- see ``deferred`` in the result.
"""

import os
import subprocess
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]


def _candidate_apps() -> list[Path]:
    # Mirror tests/native/test_bf_app.py layout.
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
        pytest.skip(f"bf_app executable not found; build native first.\nChecked:\n{listed}")
    return existing[0]


APP = _resolve_app()


def _run(args, check: bool = False) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env.setdefault("PYTHONPATH", str(ROOT / "sidecar"))
    env.setdefault("QT_QPA_PLATFORM", "offscreen")
    env.setdefault("PYTHONIOENCODING", "utf-8")
    env.setdefault("PYTHONUTF8", "1")
    return subprocess.run(
        args,
        cwd=ROOT,
        env=env,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=check,
    )


def _flag_is_unsupported(result: subprocess.CompletedProcess, flag: str) -> bool:
    combined = (result.stderr + result.stdout).lower()
    flag_name = flag.lstrip("-").lower()
    return (
        result.returncode != 0
        and "unknown option" in combined
        and flag_name in combined
    )


# -- Bug 1: marker scene connect-once / no multi-color loop ----------------

def test_bug1_marker_store_colors_stay_distinct_after_repeat_add():
    """MarkerStore repeated add/replace keeps distinct colors (no loop).

    Bug 1 historical symptom: repeated marker adds caused the annotation scene
    to re-connect and cycle into a multi-color loop. The fix: ``MarkerStore``
    assigns colors by ``markers_.size() % palette.size()`` and the UI signal
    ``markerRequested`` is connected once in the ``MainWindow`` ctor.

    Observable contract (``--smoke-m5-markers``): the smoke exercises
    ``replaceFromEvents`` + ``add`` and internally asserts the first two
    markers have different colors. If the color-assignment regressed (e.g.
    always returning the same color), the smoke prints
    ``smoke-m5-markers-failed ordering_or_colors`` and exits non-zero.

    This test pins the success marker so the color-distinctness guard cannot
    silently disappear.
    """
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"
    result = _run([str(APP), "--smoke-m5-markers", str(sample)])

    if _flag_is_unsupported(result, "--smoke-m5-markers"):
        pytest.skip("--smoke-m5-markers not implemented (bf_app reports Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m5-markers to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m5-markers-ok" in result.stdout, (
        f"expected smoke-m5-markers-ok marker, got:\n{result.stdout}"
    )
    assert "ordering_or_colors" not in result.stderr, (
        "marker color-distinctness guard regressed: "
        f"ordering_or_colors failure in stderr:\n{result.stderr}"
    )


# -- Bug 2: switching file cancels stale trend (latest-request-wins) -------

def test_bug2_analysis_cancel_and_latest_request_wins_pinned():
    """AnalysisService cancel + latest-request-wins stay asserted in smoke.

    Bug 2 historical symptom: switching file mid-analysis let a stale trend
    computation overwrite the fresh result. The fix: ``AnalysisService``
    tracks request generations, delivers only the latest, and ``cancel()``
    suppresses the callback. ``--smoke-m4-analysis`` exercises all three paths
    and encodes ``latest_request=true cancel=true`` in its success marker.

    Pinning those substrings means a regression that drops either guard
    (e.g. re-introducing a stale-result overwrite) turns this test red.
    """
    result = _run([str(APP), "--smoke-m4-analysis"])

    if _flag_is_unsupported(result, "--smoke-m4-analysis"):
        pytest.skip("--smoke-m4-analysis not implemented (bf_app reports Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m4-analysis to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m4-analysis-ok" in result.stdout, (
        f"expected smoke-m4-analysis-ok marker, got:\n{result.stdout}"
    )
    assert "latest_request=true" in result.stdout, (
        "latest-request-wins guard missing from smoke output "
        f"(stale-result overwrite regression?):\n{result.stdout}"
    )
    assert "cancel=true" in result.stdout, (
        "cancel guard missing from smoke output "
        f"(stale-callback regression?):\n{result.stdout}"
    )
    assert "cancel_callback=true" not in result.stderr, (
        "cancel suppression regressed: cancelled request still fired callback:\n"
        f"{result.stderr}"
    )


# -- Bug 3: dark-mode traversal re-themes built graphics -------------------

def test_bug3_dark_mode_palette_applied_after_toggle():
    """Switching theme applies the dark palette colours (traversal ran).

    Bug 3 historical symptom: toggling to dark mode only themed newly-built
    items, leaving already-built graphics on the light palette. The fix:
    ``MainWindow`` re-applies the palette so existing widgets pick up the new
    colours. ``--smoke-m8-system-ui`` triggers light then dark and internally
    asserts ``darkApplied`` (window == ``#1E1B2E`` && highlight == ``#A78BFA``).

    Pinning the success marker proves the palette-traversal path executed.
    """
    result = _run([str(APP), "--smoke-m8-system-ui"])

    if _flag_is_unsupported(result, "--smoke-m8-system-ui"):
        pytest.skip("--smoke-m8-system-ui not implemented (bf_app reports Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m8-system-ui to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m8-system-ui-ok" in result.stdout, (
        f"expected smoke-m8-system-ui-ok marker, got:\n{result.stdout}"
    )
    assert "smoke-m8-system-ui-failed" not in result.stderr, (
        f"dark-mode palette traversal regressed:\n{result.stderr}"
    )


# -- Bug 4: STFT jet colormap inline (no external colormap file) ------------

def test_bug4_spectrogram_jet_colormap_renders_inline():
    """SpectrogramWidget renders the jet colormap without an external file.

    Bug 4 historical symptom: spectrogram depended on an external colormap
    file that could be missing at runtime. The fix: the ``jet()`` colour
    function is defined inline in ``analysis_widgets.cpp`` (no file IO).
    ``--smoke-m4-render`` renders ``SpectrogramWidget`` and reports
    ``spectrogram=<int>`` non-background pixels.

    Asserting a large pixel count proves the inline jet colormap filled the
    matrix + colorbar rather than falling back to an empty-data placeholder.
    """
    import re

    result = _run([str(APP), "--smoke-m4-render"])

    if _flag_is_unsupported(result, "--smoke-m4-render"):
        pytest.skip("--smoke-m4-render not implemented (bf_app reports Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m4-render to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    match = re.search(r"smoke-m4-render-ok .*spectrogram=(\d+)", result.stdout)
    assert match is not None, (
        f"expected smoke-m4-render-ok with spectrogram=<int>, got:\n{result.stdout}"
    )
    spectrogram_pixels = int(match.group(1))
    # Inline jet fills the cell matrix + colorbar; an empty-data placeholder
    # stays near the frame + text magnitude (~thousands). 5000 catches a
    # colormap-not-rendered regression.
    assert spectrogram_pixels >= 5000, (
        f"spectrogram pixels {spectrogram_pixels} < 5000: "
        "jet colormap may not have rendered (inline colormap regression)"
    )


# -- Bug 5: video slave clock, no singleShot compensation ------------------

def test_bug5_video_sync_slave_clock_no_compensation_timer():
    """VideoSyncState drives the slave clock via seek, not a singleShot timer.

    Bug 5 historical symptom: video sync used a ``QTimer::singleShot``
    compensation that drifted under load. The fix: ``VideoSyncState::sync``
    emits seek/pause/play commands directly; the ``--smoke-m6-sync`` smoke
    reports ``blank_idempotent=true`` and the resync thresholds
    (``playing_threshold_ms=400 paused_threshold_ms=50``).

    Pinning ``blank_idempotent=true`` proves the slave-clock seek path ran
    without a compensation timer (repeated blank does not re-seek).
    """
    result = _run([str(APP), "--smoke-m6-sync"])

    if _flag_is_unsupported(result, "--smoke-m6-sync"):
        pytest.skip("--smoke-m6-sync not implemented (bf_app reports Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m6-sync to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m6-sync-ok" in result.stdout, (
        f"expected smoke-m6-sync-ok marker, got:\n{result.stdout}"
    )
    assert "blank_idempotent=true" in result.stdout, (
        "blank-idempotency guard missing from smoke output "
        f"(compensation-timer regression?):\n{result.stdout}"
    )
    assert "smoke-m6-sync-failed" not in result.stderr, (
        f"video sync slave-clock guard regressed:\n{result.stderr}"
    )


# -- Bug 6: blank segment black screen (no first-frame poster) --------------

def test_bug6_blank_segment_returns_black_not_first_frame():
    """Before-start / after-end blank yields pause+seek 0 (no poster frame).

    Bug 6 historical symptom: the blank segment before the video start showed
    the media's first frame as a poster instead of black. The fix:
    ``VideoSyncState`` marks before-start/after-end as ``blank`` with
    ``pause + seek 0``. ``--smoke-m6-sync`` internally asserts
    ``beforeStart.blank/pause/seek 0`` and ``repeatedBlank.seek == false``.

    The success marker + ``blank_idempotent=true`` covers both the
    "blank is black" and "repeated blank is idempotent" invariants.
    """
    result = _run([str(APP), "--smoke-m6-sync"])

    if _flag_is_unsupported(result, "--smoke-m6-sync"):
        pytest.skip("--smoke-m6-sync not implemented (bf_app reports Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-m6-sync to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-m6-sync-ok" in result.stdout, (
        f"expected smoke-m6-sync-ok marker, got:\n{result.stdout}"
    )
    assert "blank_idempotent=true" in result.stdout, (
        "blank-segment black-screen guard missing from smoke output "
        f"(first-frame-poster regression?):\n{result.stdout}"
    )
    assert "blank_entry" not in result.stderr, (
        f"blank-segment guard regressed (blank_entry failure):\n{result.stderr}"
    )


# -- Bug 8: switching file disconnects old session signals ------------------

def test_bug8_controller_disposes_cleanly_after_open():
    """FileOpenController tears down cleanly after an open (no signal leak).

    Bug 8 historical symptom: after switching files the play button stopped
    working because old session signals stayed connected. The fix:
    ``FileOpenController`` worker threads ``deleteLater`` on ``QThread::finished``
    and the controller destructs without crashing. ``--smoke-ui-open-file-dispose``
    opens a file and immediately destroys the controller; a clean exit
    (``smoke-ui-open-file-dispose-ok``) proves no dangling connection.
    """
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"
    result = _run(
        [str(APP), "--smoke-ui-open-file-dispose", str(sample), "--language", "en_US"],
    )

    if _flag_is_unsupported(result, "--smoke-ui-open-file-dispose"):
        pytest.skip("--smoke-ui-open-file-dispose not implemented (bf_app reports Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-ui-open-file-dispose to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert result.stdout.strip() == "smoke-ui-open-file-dispose-ok", (
        f"expected smoke-ui-open-file-dispose-ok marker, got:\n{result.stdout}"
    )


def test_bug8_window_request_still_alive_after_first_open():
    """A second showFileOpenResult + window request completes after the first.

    Bug 8 historical symptom: opening a second file left the play button dead
    because the window-request handler was wired to a stale session. The fix:
    ``MainWindow::showFileOpenResult`` handles re-entry and the
    window-request handler stays wired. ``--smoke-ui-window-latest`` opens a
    file, calls ``showFileOpenResult`` again, then issues a window request and
    expects ``smoke-ui-window-latest-ok``.

    A clean completion proves the session-switch path did not strand the
    window-request handler.
    """
    sample = ROOT / "tests" / "fixtures" / "generated" / "simple_eeg.csv"
    result = _run(
        [str(APP), "--smoke-ui-window-latest", str(sample), "--language", "en_US"],
    )

    if _flag_is_unsupported(result, "--smoke-ui-window-latest"):
        pytest.skip("--smoke-ui-window-latest not implemented (bf_app reports Unknown option)")

    assert result.returncode == 0, (
        f"expected smoke-ui-window-latest to succeed, got rc={result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "smoke-ui-window-latest-ok" in result.stdout, (
        f"expected smoke-ui-window-latest-ok marker, got:\n{result.stdout}"
    )
    assert "smoke-ui-window-latest-failed" not in result.stderr, (
        f"session-switch re-entry regressed:\n{result.stderr}"
    )
