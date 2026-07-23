# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for the BoundFree EEG Python sidecar.

The sidecar ships as a console onedir bundle so the native host can launch it
as ``boundfree_sidecar.exe --stdio`` without a Python interpreter on the
target machine. MNE pulls in a large forest of lazily-imported submodules plus
native extensions (scipy, matplotlib backend bits); ``collect_all('mne')`` is
mandatory so every format reader remains callable in the frozen tree.

Historical constraint: ``matplotlib`` MUST stay bundled. MNE depends on it for
its report/plotting code paths, and earlier frozen builds broke when it was
stripped. It is collected transitively via ``collect_all('mne')`` and is not
listed in ``excludes``.

Repository assets (``version.json``, ``contracts/error_codes.json``) are added
as data files so the frozen executable can resolve them from its own directory
tree via ``repo_root()`` (see ``boundfree_sidecar/version.py``).
"""

from pathlib import Path

from PyInstaller.utils.hooks import collect_all

block_cipher = None

# MNE is the heavy dependency: it owns scipy/numpy/matplotlib/pyedflib style
# format backends and uses lazy_loader everywhere. collect_all grabs modules,
# data files and binary extensions in one shot.
mne_datas, mne_binaries, mne_hiddenimports = collect_all("mne")

# h5py/pyedflib carry native shared objects that PyInstaller's analyzer misses
# under certain import orders; collect them explicitly.
h5py_datas, h5py_binaries, h5py_hiddenimports = collect_all("h5py")
pyedflib_datas, pyedflib_binaries, pyedflib_hiddenimports = collect_all("pyedflib")
numpy_datas, numpy_binaries, numpy_hiddenimports = collect_all("numpy")

a = Analysis(
    ["frozen_main.py"],
    pathex=["."],
    binaries=(
        mne_binaries
        + h5py_binaries
        + pyedflib_binaries
        + numpy_binaries
    ),
    datas=(
        mne_datas
        + h5py_datas
        + pyedflib_datas
        + numpy_datas
        + [
            ("../version.json", "."),
            ("../contracts/error_codes.json", "contracts"),
        ]
    ),
    hiddenimports=(
        mne_hiddenimports
        + h5py_hiddenimports
        + pyedflib_hiddenimports
        + numpy_hiddenimports
        + ["h5py", "pyedflib", "numpy", "matplotlib", "pkg_resources._vendor.appdirs"]
    ),
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        # Heavy dev/test tooling that never ships in a release sidecar.
        "pytest",
        "IPython",
        "jupyter",
        "notebook",
        "tkinter",
        "PySide6",
        "pyqtgraph",
        "cryptography",
    ],
    win_no_prefer_redirect=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="boundfree_sidecar",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="boundfree_sidecar",
)
