"""Regression guard for the sidecar PyInstaller spec (legacy AGENTS.md bug 7).

Bug 7 historical symptom: a frozen sidecar build shipped a stub that could not
read MNE formats because ``collect_all('mne')`` was dropped from the spec,
leaving lazily-imported MNE submodules + native extensions (scipy,
matplotlib, pyedflib) unpackaged. The fix: the spec calls
``collect_all('mne')`` (and ``collect_all('h5py'/'pyedflib'/'numpy')``) and
keeps ``matplotlib`` bundled (it is transitively collected via mne and must
NOT appear in ``excludes``).

This module statically asserts the spec's packaging invariants without running
PyInstaller, so a regression that turns the spec into a packaging stub is
caught before a release build.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / "sidecar" / "boundfree_sidecar.spec"


def _spec_text() -> str:
    return SPEC.read_text(encoding="utf-8")


def test_spec_collects_all_mne():
    """``collect_all('mne')`` must be present so every MNE format reader ships.

    A regression that removes this call (or replaces it with a narrower
    collect) would silently produce a stub sidecar that crashes on real EEG
    formats. The regex tolerates quote-style changes.
    """
    assert re.search(r"collect_all\s*\(\s*['\"]mne['\"]\s*\)", _spec_text()), (
        "boundfree_sidecar.spec lost collect_all('mne'): frozen build would ship a stub"
    )


def test_spec_mne_collected_outputs_feed_analysis():
    """The mne collect_all outputs must actually flow into Analysis (binaries,
    datas, hiddenimports), not just be computed and discarded.

    Guards against a regression that calls ``collect_all('mne')`` for its side
    effects but forgets to wire ``mne_binaries``/``mne_datas``/
    ``mne_hiddenimports`` into the Analysis tuple.
    """
    text = _spec_text()
    assert "mne_binaries" in text, "mne_binaries not wired into Analysis"
    assert "mne_datas" in text, "mne_datas not wired into Analysis"
    assert "mne_hiddenimports" in text, "mne_hiddenimports not wired into Analysis"


def test_spec_matplotlib_not_excluded():
    """matplotlib must stay bundled (MNE depends on it for report/plotting).

    A regression that adds ``matplotlib`` to the ``excludes`` list would break
    frozen MNE paths even though ``collect_all('mne')`` is present.
    """
    text = _spec_text()
    m = re.search(r"excludes\s*=\s*\[([^\]]*)\]", text, re.DOTALL)
    if not m:
        return  # no excludes list at all -> nothing to guard
    excluded = [s.strip().strip("'\"") for s in m.group(1).split(",") if s.strip()]
    assert "matplotlib" not in excluded, (
        "matplotlib is in excludes: frozen MNE report/plotting would break"
    )


def test_spec_keeps_onedir_mode():
    """The sidecar ships as onedir (console exe + COLLECT), not onefile.

    A regression that flips to onefile (``exclude_binaries=False`` + no
    ``COLLECT``) would change the launch contract the native host depends on
    (``boundfree_sidecar.exe --stdio`` resolving siblings in its own dir).
    """
    text = _spec_text()
    assert "exclude_binaries=True" in text, "spec lost exclude_binaries=True (onedir contract)"
    assert re.search(r"\bCOLLECT\s*\(", text), "spec lost COLLECT() (onedir contract)"
