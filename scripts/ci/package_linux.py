#!/usr/bin/env python3
"""Bundle the installed app and Qt runtime for Ubuntu 24.04 x86_64."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile

root = Path.cwd()
qt = Path(os.environ['QT_ROOT_DIR'])
stage = root / 'release/stage/BoundFreeEEG'
subprocess.run(['cmake', '--install', 'native/build', '--prefix', str(stage)], check=True)
shutil.copytree(root / 'release/sidecar-dist/boundfree_sidecar', stage / 'sidecar', dirs_exist_ok=True)
for category in ('platforms', 'platforminputcontexts', 'imageformats', 'multimedia',
                 'tls', 'xcbglintegrations', 'wayland-shell-integration',
                 'wayland-decoration-client', 'wayland-graphics-integration-client',
                 'egldeviceintegrations'):
    source = qt / 'plugins' / category
    if source.exists():
        shutil.copytree(source, stage / 'plugins' / category, dirs_exist_ok=True)
shutil.copytree(qt / 'translations', stage / 'translations', dirs_exist_ok=True)
shutil.copy2('build-info.json', stage)
libs = stage / 'lib'
libs.mkdir(exist_ok=True)
# Resolve SONAME links, retaining both canonical and requested file names.
queue = [stage / 'bf_app', *stage.glob('plugins/**/*.so')]
seen = set()
while queue:
    binary = queue.pop()
    if str(binary) in seen:
        continue
    seen.add(str(binary))
    result = subprocess.run(['ldd', str(binary)], capture_output=True, text=True)
    if 'not found' in result.stdout:
        raise SystemExit(f'unresolved runtime dependency in {binary}: {result.stdout}')
    for name, path in re.findall(r'^\s*(\S+) => (/\S+)', result.stdout, re.M):
        # Keep glibc/loader supplied by the documented target OS.
        if re.match(r'lib(c|m|dl|pthread|rt|resolv|util)\.so', name):
            continue
        target = libs / name
        if not target.exists():
            shutil.copy2(path, target)
            queue.append(target)
launcher = stage / 'BoundFreeEEG.sh'
launcher.write_text('''#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$app_dir/plugins"
export BF_RESOURCE_ROOT="$app_dir"
exec "$app_dir/bf_app" "$@"
''')
launcher.chmod(0o755)
(stage / 'README-linux.txt').write_text('Ubuntu 24.04 x86_64 build. Extract and run ./BoundFreeEEG.sh.\n'
    'Requires a Linux desktop, system graphics drivers, X11/Wayland and audio services.\n'
    'Other distributions and real GPU operation require separate acceptance.\n')
version = json.loads((stage / 'version.json').read_text())['version']
Path('dist').mkdir(exist_ok=True)
with tarfile.open(f'dist/BoundFreeEEG_{version}_ubuntu24.04-x86_64.tar.gz', 'w:gz') as archive:
    archive.add(stage, arcname='BoundFreeEEG')
# Verify the archive itself from a fresh, source-independent directory.
import tempfile
with tempfile.TemporaryDirectory() as temp:
    with tarfile.open(f'dist/BoundFreeEEG_{version}_ubuntu24.04-x86_64.tar.gz') as archive:
        archive.extractall(temp, filter='data')
    app = Path(temp) / 'BoundFreeEEG/BoundFreeEEG.sh'
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    env.pop('BF_PYTHON_EXECUTABLE', None)
    env.pop('PYTHONPATH', None)
    for args in (['--version'], ['--smoke-sidecar'], ['--smoke-ui-workspace-navigation']):
        subprocess.run([str(app), *args], cwd=temp, env=env, check=True, timeout=180)
