#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FAST=0

if [[ "${1:-}" == "--fast" ]]; then
  FAST=1
fi

cd "$ROOT"

python3 -m json.tool version.json >/dev/null
python3 -m json.tool contracts/error_codes.json >/dev/null
python3 scripts/check_i18n.py

if [[ "$FAST" -eq 0 ]]; then
  (
    cd native
    cmake --preset wsl2-qt6-conda
    cmake --build --preset wsl2-qt6-conda
  )
fi

python3 -m pytest tests -q --ignore=tests/scripts

if [[ "$FAST" -eq 0 ]]; then
  native/build/bf_app --version
  PYTHONPATH="$ROOT/sidecar${PYTHONPATH:+:$PYTHONPATH}" \
    native/build/bf_app --smoke-sidecar
  for locale in zh_CN en_US pseudo; do
    QT_QPA_PLATFORM=offscreen \
      PYTHONPATH="$ROOT/sidecar${PYTHONPATH:+:$PYTHONPATH}" \
      native/build/bf_app --smoke-ui --language "$locale"
  done
fi

echo "M1 foundation verification passed"
