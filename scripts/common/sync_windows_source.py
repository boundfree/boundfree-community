#!/usr/bin/env python3
"""Synchronize a Linux source checkout into a guarded Windows build mirror.

The mirror manifest records the previous source bytes, including uncommitted
files. Destination edits are rejected before source files are overwritten.
Machine-local configuration, build outputs and private material stay local.
"""
import argparse
from contextlib import contextmanager
import fnmatch
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile

MANIFEST = '.boundfree-windows-mirror.json'
LOCK = '.boundfree-sync.lock'
LOCAL_FILES = {MANIFEST, LOCK, 'config/windows-dev.local.json',
               'native/CMakeUserPresets.json', 'build-info.json', 'license.dat'}


def excluded(relative):
    path = PurePosixPath(relative)
    parts = path.parts
    if relative in LOCAL_FILES:
        return True
    if any(p in {'.git', '.venv', '__pycache__', '.pytest_cache', 'node_modules'}
           or p.endswith(('.egg-info', '_autogen')) for p in parts):
        return True
    if parts and parts[0] in {'release', 'dist'}:
        return True
    if len(parts) > 1 and parts[0] == 'native' and (parts[1] == 'build' or parts[1].startswith('build-')):
        return True
    if len(parts) > 2 and parts[:2] == ('native', 'spike') and (parts[2] == 'build' or parts[2].startswith('build-')):
        return True
    if parts[:2] in {('installer', 'Output'), ('sidecar', 'build')}:
        return True
    lower = path.name.lower()
    return (lower.endswith(('.pyc', '.pfx', '.exe', '.msi')) or lower == 'private_key.pem'
            or fnmatch.fnmatch(lower, '*private*key*'))


def safe_path(root, relative):
    path = PurePosixPath(relative)
    if path.is_absolute() or not path.parts or any(p in {'.', '..'} for p in path.parts) or '\\' in relative:
        raise ValueError('Invalid source path: ' + relative)
    result = root
    for part in path.parts:
        result = result / part
        if result.is_symlink():
            raise ValueError('Source path traverses a symlink: ' + str(result))
    return result


def file_names(root):
    probe = subprocess.run(['git', '-C', str(root), 'rev-parse', '--show-toplevel'], capture_output=True, text=True)
    if probe.returncode == 0 and Path(probe.stdout.strip()).resolve() == root:
        result = subprocess.run(['git', '-C', str(root), 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
                                check=True, capture_output=True)
        names = [os.fsdecode(p) for p in result.stdout.split(b'\0') if p]
    else:
        names = []
        for folder, dirs, files in os.walk(root, followlinks=False):
            parent = Path(folder)
            dirs[:] = [p for p in dirs if not excluded((parent / p).relative_to(root).as_posix())]
            for name in files:
                names.append((parent / name).relative_to(root).as_posix())
    return sorted({name for name in names if not excluded(name)})


def fingerprint(path):
    if not path.is_file() or path.is_symlink():
        raise ValueError('Expected a regular source file: ' + str(path))
    data = path.read_bytes()
    return {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}


def snapshot(root):
    result = {}
    folded = set()
    for name in file_names(root):
        path = safe_path(root, name)
        if not path.exists():
            continue  # A tracked deletion is intentionally absent in the mirror.
        if name.casefold() in folded:
            raise ValueError('Windows case-insensitive source collision: ' + name)
        folded.add(name.casefold())
        result[name] = fingerprint(path)
    return result


def load_manifest(target):
    path = target / MANIFEST
    if path.is_symlink():
        raise ValueError('Mirror manifest must not be a symlink')
    if not path.exists():
        return None
    value = json.loads(path.read_text(encoding='utf-8'))
    if value.get('schema_version') != 1 or not isinstance(value.get('files'), dict):
        raise ValueError('Invalid Windows mirror manifest')
    for name, entry in value['files'].items():
        safe_path(target, name)
        if excluded(name) or not isinstance(entry, dict) or len(entry.get('sha256', '')) != 64 or not isinstance(entry.get('size'), int):
            raise ValueError('Invalid manifest entry: ' + name)
    return value


def verify_target(target, before):
    actual = snapshot(target)
    changed = sorted(name for name in set(actual) | set(before) if actual.get(name) != before.get(name))
    if changed:
        raise ValueError('Windows build mirror has local changes; preserve them before syncing:\n  ' + '\n  '.join(changed[:30]))


def write_manifest(target, value):
    fd, name = tempfile.mkstemp(prefix='.boundfree-manifest-', dir=target)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as file:
            json.dump(value, file, ensure_ascii=False, indent=2)
            file.write('\n')
        os.replace(name, target / MANIFEST)
    finally:
        if os.path.exists(name):
            os.unlink(name)


@contextmanager
def mirror_lock(target):
    path = target / LOCK
    try:
        fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    except FileExistsError as exc:
        raise ValueError('A mirror sync is already active: ' + str(path)) from exc
    try:
        os.write(fd, str(os.getpid()).encode())
        os.close(fd)
        yield
    finally:
        path.unlink()


def synchronize(source, target, *, initialize=False, dry_run=False):
    source, target = Path(source).resolve(), Path(target).resolve()
    if (source / MANIFEST).exists():
        raise ValueError('A Windows build mirror cannot be used as the development source')
    if source == target or source in target.parents or target in source.parents:
        raise ValueError('Source and build mirror must be separate directories')
    if not (source / 'version.json').is_file():
        raise ValueError('Development source is missing version.json')
    if not target.exists():
        if not initialize:
            raise ValueError('Build mirror is missing; use --init for a new empty directory')
        if not dry_run:
            target.mkdir(parents=True)
    if target.exists() and not target.is_dir():
        raise ValueError('Build mirror must be a directory')
    state = load_manifest(target) if target.exists() else None
    if state is None:
        if not initialize or (target.exists() and snapshot(target)):
            raise ValueError('Unmanaged or non-empty Windows target; preserve it and initialize a new empty mirror with --init')
        before = {}
    else:
        if state.get('source_root') != str(source):
            raise ValueError('Mirror belongs to a different development source: ' + str(state.get('source_root')))
        before = state['files']
        verify_target(target, before)
    after = snapshot(source)
    changed = sorted(name for name in after if after[name] != before.get(name))
    removed = sorted(set(before) - set(after))
    if dry_run:
        return {'changed': changed, 'removed': removed, 'files': len(after), 'dry_run': True}
    with mirror_lock(target), tempfile.TemporaryDirectory(prefix='boundfree-source-sync-') as directory:
        staging = Path(directory)
        for name in changed:
            incoming = staging / 'incoming' / name
            incoming.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(safe_path(source, name), incoming)
            if fingerprint(incoming) != after[name]:
                raise ValueError('Source changed during synchronization: ' + name)
        # Recheck after staging, before touching destination source files.
        if state is not None:
            verify_target(target, before)
        if snapshot(source) != after:
            raise ValueError('Source changed during synchronization; retry when editing stops')
        for name in changed + removed:
            if name in before:
                backup = staging / 'previous' / name
                backup.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(safe_path(target, name), backup)
        touched = []
        try:
            for name in changed:
                destination = safe_path(target, name)
                destination.parent.mkdir(parents=True, exist_ok=True)
                touched.append(name)
                shutil.copy2(staging / 'incoming' / name, destination)
            for name in removed:
                touched.append(name)
                safe_path(target, name).unlink()
            if snapshot(target) != after:
                raise ValueError('Mirror verification failed after copying')
            head = subprocess.run(['git', '-C', str(source), 'rev-parse', 'HEAD'], capture_output=True, text=True)
            write_manifest(target, {'schema_version': 1, 'role': 'windows-build-mirror',
                                   'source_root': str(source), 'source_head': head.stdout.strip() if head.returncode == 0 else None,
                                   'files': after})
        except BaseException:
            for name in reversed(touched):
                destination = safe_path(target, name)
                if name in before:
                    shutil.copy2(staging / 'previous' / name, destination)
                else:
                    destination.unlink(missing_ok=True)
            raise
    return {'changed': changed, 'removed': removed, 'files': len(after), 'dry_run': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('target', type=Path)
    parser.add_argument('--init', action='store_true')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    try:
        result = synchronize(args.source, args.target, initialize=args.init, dry_run=args.dry_run)
    except (OSError, ValueError, subprocess.SubprocessError) as exc:
        parser.exit(1, str(exc) + '\n')
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
