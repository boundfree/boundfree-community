#!/usr/bin/env bash
# Synchronize reviewable source from WSL2 to a Windows working copy.
# Windows owns its .venv, native/build-*, release, dist, and all generated files.
set -euo pipefail

source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
target_root=${1:-/mnt/c/Users/Admin/Desktop/项目/BoudFree/boundfree-eeg-native}

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "sync_to_windows.sh must run in WSL2/Linux." >&2
  exit 1
fi
if [[ ! -d "$target_root" ]]; then
  echo "Windows repository not found: $target_root" >&2
  exit 1
fi
if [[ ! -f "$target_root/version.json" ]]; then
  echo "Target is not a BoundFree EEG Native repository: $target_root" >&2
  exit 1
fi

rsync -a --delete \
  --exclude='.git/' \
  --exclude='.venv/' \
  --exclude='native/spike/build/' \
  --exclude='native/spike/build-*/' \
  --exclude='native/build/' \
  --exclude='native/build-*/' \
  --exclude='native/**/*_autogen/' \
  --exclude='release/' \
  --exclude='dist/' \
  --exclude='installer/Output/' \
  --exclude='__pycache__/' \
  --exclude='.pytest_cache/' \
  --exclude='*.pyc' \
  --exclude='private_key.pem' \
  --exclude='*private*key*' \
  --exclude='*.pfx' \
  --exclude='license.dat' \
  "$source_root/" "$target_root/"

printf 'Synchronized WSL source to Windows repository: %s\n' "$target_root"
