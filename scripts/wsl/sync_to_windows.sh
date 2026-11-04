#!/usr/bin/env bash
# Synchronize reviewable source from WSL2 to a Windows working copy.
# Windows owns its .venv, native/build-*, release, dist, and all generated files.
set -euo pipefail

source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
config_path="$source_root/config/windows-dev.local.json"

init_mode=false
dry_run=false
target_root=""

for arg in "$@"; do
  case "$arg" in
    --dry-run)
      dry_run=true
      ;;
    --init)
      init_mode=true
      ;;
    -*)
      echo "Unknown option: $arg" >&2
      exit 1
      ;;
    *)
      if [[ -z "$target_root" ]]; then
        target_root="$arg"
      else
        echo "Unexpected argument: $arg" >&2
        exit 1
      fi
      ;;
  esac
done

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "sync_to_windows.sh must run in WSL2/Linux." >&2
  exit 1
fi

if [[ -z "$target_root" && -f "$config_path" ]]; then
  target_root=$(python3 - "$config_path" <<'PY'
import json
import sys

path = sys.argv[1]
try:
    data = json.load(open(path, encoding="utf-8"))
except (OSError, json.JSONDecodeError) as exc:
    raise SystemExit(f"Invalid Windows development config {path}: {exc}")

if data.get("schema_version") != 1:
    raise SystemExit(f"Unsupported Windows development config schema_version: {data.get('schema_version')!r}")

value = data.get("wsl_sync_target")
if not isinstance(value, str) or not value:
    raise SystemExit("windows-dev.local.json must define a non-empty wsl_sync_target")
print(value)
PY
)
fi

if [[ -z "$target_root" ]]; then
  echo "Windows repository target is required. Copy config/windows-dev.example.json to config/windows-dev.local.json and set wsl_sync_target, or pass it as an argument." >&2
  exit 1
fi

if [[ ! -d "$target_root" ]]; then
  if [[ "$init_mode" == true ]]; then
    if [[ "$dry_run" == false ]]; then mkdir -p "$target_root"; fi
  else
    echo "Windows repository not found: $target_root" >&2
    exit 1
  fi
fi

if [[ "$init_mode" == false && ! -f "$target_root/version.json" ]]; then
  echo "Target is not a BoundFree EEG Native repository: $target_root. Use --init to initialize an empty directory." >&2
  exit 1
fi

sync_options=()
if [[ "$init_mode" == true ]]; then sync_options+=(--init); fi
if [[ "$dry_run" == true ]]; then sync_options+=(--dry-run); fi
python3 "$source_root/scripts/common/sync_windows_source.py" \
  "$source_root" "$target_root" "${sync_options[@]}"
