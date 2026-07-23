"""Frozen entry point for the BoundFree EEG sidecar.

``__main__.py`` uses a relative import (``from .cli import main``) which only
works when executed as ``python -m boundfree_sidecar``. PyInstaller runs the
entry script as a top-level ``__main__`` module with no package context, so
the relative import fails. This shim uses an absolute import and is what the
PyInstaller spec points at; it remains import-safe under ``python -m`` too.
"""

from boundfree_sidecar.cli import main

if __name__ == "__main__":
    raise SystemExit(main())
