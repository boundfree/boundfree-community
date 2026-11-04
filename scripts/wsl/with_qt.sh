#!/usr/bin/env bash
# Run one command with Community's Qt, without activating or replacing other environments.
set -euo pipefail
bf_repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bf_common="$(git -C "$bf_repo" rev-parse --path-format=absolute --git-common-dir)"
bf_workspace="$(cd "$bf_common/../.." && pwd)"
export BF_QT_ROOT="${BF_QT_ROOT:-$bf_workspace/toolchains/qt6-community}"
if [[ ! -f "$BF_QT_ROOT/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    printf 'Community Qt not found: %s; see docs/toolchain-wsl2.md\n' "$BF_QT_ROOT" >&2
    exit 1
fi
if [[ $# -eq 0 ]]; then
    printf 'Usage: %s command [arguments...]\n' "$0" >&2
    exit 2
fi
export PATH="$BF_QT_ROOT/bin:$PATH"
if [[ -x "$BF_QT_ROOT/bin/python" ]]; then
    export BF_PYTHON_EXECUTABLE="${BF_PYTHON_EXECUTABLE:-$BF_QT_ROOT/bin/python}"
fi
export LD_LIBRARY_PATH="$BF_QT_ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML_IMPORT_PATH QML2_IMPORT_PATH
if [[ -d "$BF_QT_ROOT/lib/qt6/plugins" ]]; then
    export QT_PLUGIN_PATH="$BF_QT_ROOT/lib/qt6/plugins"
    export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
elif [[ -d "$BF_QT_ROOT/plugins" ]]; then
    export QT_PLUGIN_PATH="$BF_QT_ROOT/plugins"
    export QT_QPA_PLATFORM_PLUGIN_PATH="$QT_PLUGIN_PATH/platforms"
fi
exec "$@"
