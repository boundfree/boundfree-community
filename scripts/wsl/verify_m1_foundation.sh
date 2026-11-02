#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FAST=0
NATIVE_BUILD_DIR="${BF_NATIVE_BUILD_DIR:-$ROOT/native/build-qt6-baseline}"
PYTHON="${BF_PYTHON_EXECUTABLE:-python3}"

if [[ "${1:-}" == "--fast" ]]; then
  FAST=1
fi

cd "$ROOT"

"$PYTHON" -m json.tool version.json >/dev/null
"$PYTHON" -m json.tool contracts/error_codes.json >/dev/null
"$PYTHON" scripts/common/check_i18n.py

if [[ "$FAST" -eq 0 ]]; then
  (
    cd native
    cmake --preset wsl2-qt6-conda -B "$NATIVE_BUILD_DIR"
    cmake --build "$NATIVE_BUILD_DIR"
  )
fi

"$PYTHON" -m pytest tests -q --ignore=tests/scripts

if [[ "$FAST" -eq 0 ]]; then
  "$NATIVE_BUILD_DIR/bf_app" --version
  PYTHONPATH="$ROOT/sidecar${PYTHONPATH:+:$PYTHONPATH}" \
    "$NATIVE_BUILD_DIR/bf_app" --smoke-sidecar
  for locale in zh_CN en_US pseudo; do
    QT_QPA_PLATFORM=offscreen \
      PYTHONPATH="$ROOT/sidecar${PYTHONPATH:+:$PYTHONPATH}" \
      "$NATIVE_BUILD_DIR/bf_app" --smoke-ui --language "$locale"
  done
fi

echo "M1 foundation verification passed"
