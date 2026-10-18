#!/usr/bin/env bash
# One-shot WSL entry for syncing source to Windows, compiling native MSVC release,
# freezing Python sidecar, packaging Inno Setup installer, and retrieving the artifact.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
CONFIG_PATH="$REPO_ROOT/config/windows-dev.local.json"

FAST_MODE=false
SKIP_VERIFY=false
SKIP_INSTALLER=false
SKIP_SYNC=false

show_help() {
  cat <<EOF
Usage: ./scripts/wsl/build_windows_dist.sh [options]

Options:
  --fast             Fast incremental mode (skip sidecar refreeze and pip sync)
  --skip-verify      Skip automated smoke verification (faster artifact packaging)
  --skip-installer   Skip Inno Setup installer creation (build and stage binaries only)
  --no-sync          Skip syncing WSL source to Windows working directory
  -h, --help         Show this help message
EOF
}

for arg in "$@"; do
  case "$arg" in
    --fast)
      FAST_MODE=true
      ;;
    --skip-verify)
      SKIP_VERIFY=true
      ;;
    --skip-installer)
      SKIP_INSTALLER=true
      ;;
    --no-sync)
      SKIP_SYNC=true
      ;;
    -h|--help)
      show_help
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg" >&2
      show_help
      exit 1
      ;;
  esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "Error: build_windows_dist.sh must run inside WSL2/Linux." >&2
  exit 1
fi

if [[ ! -f "$CONFIG_PATH" ]]; then
  echo "Error: Configuration file not found: $CONFIG_PATH" >&2
  echo "Please copy config/windows-dev.example.json to config/windows-dev.local.json." >&2
  exit 1
fi

# Locate powershell.exe
POWERSHELL_EXE=""
for candidate in \
  "powershell.exe" \
  "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe" \
  "/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"; do
  if command -v "$candidate" >/dev/null 2>&1 || [[ -x "$candidate" ]]; then
    POWERSHELL_EXE="$candidate"
    break
  fi
done

if [[ -z "$POWERSHELL_EXE" ]]; then
  echo "Error: Windows PowerShell executable not found." >&2
  exit 1
fi

# Read target from config
WIN_TARGET=$(python3 - "$CONFIG_PATH" <<'PY'
import json, sys
try:
    cfg = json.load(open(sys.argv[1], encoding='utf-8'))
    print(cfg.get("wsl_sync_target", ""))
except Exception as e:
    sys.exit(str(e))
PY
)

if [[ -z "$WIN_TARGET" || ! -d "$WIN_TARGET" ]]; then
  echo "Error: Invalid or non-existent wsl_sync_target in $CONFIG_PATH: $WIN_TARGET" >&2
  exit 1
fi

# Read version
VERSION=$(python3 - "$REPO_ROOT/version.json" <<'PY'
import json, sys
try:
    v = json.load(open(sys.argv[1], encoding='utf-8'))
    print(v.get("version", ""))
except Exception as e:
    sys.exit(str(e))
PY
)

echo "============================================================"
echo "BoundFree EEG - Windows Dist Pipeline (WSL Driver)"
echo "  Version:     $VERSION"
echo "  WSL Source:  $REPO_ROOT"
echo "  Win Target:  $WIN_TARGET"
echo "  Fast Mode:   $FAST_MODE"
echo "  Skip Verify: $SKIP_VERIFY"
echo "============================================================"

# Step 1: Sync to Windows
if [[ "$SKIP_SYNC" == false ]]; then
  echo ""
  echo "==> [Step 1/3] Syncing source to Windows working copy..."
  "$SCRIPT_DIR/sync_to_windows.sh" "$WIN_TARGET"
else
  echo ""
  echo "==> [Step 1/3] Syncing skipped (--no-sync)"
fi

# Step 2: Build inside Windows PowerShell
echo ""
echo "==> [Step 2/3] Invoking Windows build pipeline..."

WIN_TARGET_WIN=$(wslpath -w "$WIN_TARGET")
PS_ARGS=()
if [[ "$FAST_MODE" == true ]]; then
  PS_ARGS+=("-SkipSidecarFreeze" "-SkipPip")
fi
if [[ "$SKIP_VERIFY" == true ]]; then
  PS_ARGS+=("-SkipVerification")
fi
if [[ "$SKIP_INSTALLER" == true ]]; then
  PS_ARGS+=("-SkipInstaller")
fi

WIN_TARGET_PS=${WIN_TARGET_WIN//\'/\'\'}
PS_CMD="\$ErrorActionPreference = 'Stop'; Set-Location -LiteralPath '$WIN_TARGET_PS'; try { & .\scripts\windows\build_windows.ps1 ${PS_ARGS[*]}; exit 0 } catch { [Console]::Error.WriteLine(\$_); exit 1 }"

"$POWERSHELL_EXE" -NoProfile -ExecutionPolicy Bypass -Command "$PS_CMD"

# Step 3: Copy installer back to WSL dist/
INSTALLER_NAME="BoundFreeEEG_Setup_${VERSION}.exe"
WIN_INSTALLER="$WIN_TARGET/dist/$INSTALLER_NAME"
WSL_DIST="$REPO_ROOT/dist"

if [[ "$SKIP_INSTALLER" == false && -f "$WIN_INSTALLER" ]]; then
  echo ""
  echo "==> [Step 3/3] Retrieving installer to WSL dist/..."
  mkdir -p "$WSL_DIST"
  cp -f "$WIN_INSTALLER" "$WSL_DIST/$INSTALLER_NAME"

  SIZE_BYTES=$(stat -c%s "$WSL_DIST/$INSTALLER_NAME" 2>/dev/null || stat -f%z "$WSL_DIST/$INSTALLER_NAME")
  SIZE_MB=$(awk "BEGIN {printf \"%.2f\", $SIZE_BYTES/1048576}")
  SHA256=$(sha256sum "$WSL_DIST/$INSTALLER_NAME" | cut -d' ' -f1)

  echo "============================================================"
  echo "Build & Packaging Successful!"
  echo "  Artifact (Windows): $WIN_INSTALLER"
  echo "  Artifact (WSL):     $WSL_DIST/$INSTALLER_NAME"
  echo "  Size:               $SIZE_BYTES bytes (${SIZE_MB} MB)"
  echo "  SHA256:             $SHA256"
  echo "============================================================"
elif [[ "$SKIP_INSTALLER" == true ]]; then
  echo ""
  echo "==> [Step 3/3] Installer generation skipped as requested."
else
  echo "Error: Installer artifact not found at $WIN_INSTALLER" >&2
  exit 1
fi
