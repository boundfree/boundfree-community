#!/usr/bin/env bash
# Validate source in WSL2 and write the exact Windows release handoff.
# This script never runs Windows build tools and never copies private keys.
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "prepare_windows_handoff.sh must run in WSL2/Linux." >&2
  exit 1
fi

python3 scripts/version_sync.py check

pushd native >/dev/null
cmake --preset wsl2-qt6-conda
cmake --build --preset wsl2-qt6-conda
popd >/dev/null
python3 scripts/check_release_assets.py
python3 scripts/check_i18n.py
PYTHONPATH=sidecar python3 -m pytest tests -q

if [[ ! -f license_public.pem ]]; then
  echo "Missing license_public.pem. Export only the RSA-2048 public key; never copy a private key." >&2
  exit 1
fi

version=$(python3 -c 'import json; print(json.load(open("version.json", encoding="utf-8"))["version"])')
channel=$(python3 -c 'import json; print(json.load(open("version.json", encoding="utf-8"))["release_channel"])')
public_key_sha256=$(sha256sum license_public.pem | cut -d' ' -f1)
handoff_dir="release/windows-handoff/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$handoff_dir"

cat > "$handoff_dir/README.txt" <<EOF
BoundFree EEG Windows build handoff
version=$version
channel=$channel
license_public.pem SHA-256=$public_key_sha256

1. Synchronize source to the Windows repository. Include source, tests, docs,
   scripts, version.json, contracts, legal, installer, sidecar, native, and
   license_public.pem.
2. Do not synchronize .git, .venv, native/build*, release, dist, __pycache__,
   .pytest_cache, private_key.pem, any *private*key*, any PFX, or license.dat.
3. On Windows, use a normal non-administrator PowerShell session in the synced
   repository. Create a local .venv and install build dependencies:

   .\.venv\Scripts\python.exe -m pip install -e ".\sidecar[formats]" pyinstaller

4. Build only on Windows:

   .\build_release.ps1 -QtRoot "E:\QT\6.8.2\msvc2022_64" -Python ".\.venv\Scripts\python.exe" -LicensePublicKeyFile ".\license_public.pem" -SkipSigning

5. Then run Windows acceptance:

   .\scripts\verify_windows_release.ps1

Unsigned output is dev-only. Do not set rc or stable without Authenticode signing.
EOF

printf 'WSL preflight passed. Windows handoff: %s\n' "$handoff_dir/README.txt"
