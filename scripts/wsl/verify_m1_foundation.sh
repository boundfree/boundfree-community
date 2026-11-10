#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
FAST=0
NATIVE_BUILD_DIR="${BF_NATIVE_BUILD_DIR:-$ROOT/native/build-qt6-baseline}"
PYTHON="${BF_PYTHON_EXECUTABLE:-python3}"
# 内层 pytest 默认包含授权门禁构建。外层已经跑过一次时设 1, 避免重复最耗内存的步骤。
SKIP_LICENSE_GATE="${BF_VERIFY_SKIP_LICENSE_GATE:-0}"

if [[ "${1:-}" == "--fast" ]]; then
  FAST=1
fi

# 相对构建目录按调用者当前目录展开, 与用户传入时的含义一致; 否则下面 cd 之后
# 会被解释成相对仓库根或 native/ 的路径。
if [[ "$NATIVE_BUILD_DIR" != /* ]]; then
  NATIVE_BUILD_DIR="$PWD/$NATIVE_BUILD_DIR"
fi

cd "$ROOT"

# --fast 只跳过构建与界面自检, 内层回归仍然照跑; 数组保留参数边界。
PYTEST_ARGS=(-q --ignore=tests/scripts)
if [[ "$SKIP_LICENSE_GATE" == "1" ]]; then
  PYTEST_ARGS+=(--deselect tests/native/test_m8_license_gate.py)
fi

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

"$PYTHON" -m pytest tests "${PYTEST_ARGS[@]}"

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
