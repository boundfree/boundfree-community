"""Behavioral protection for a Windows copy that is never a source authority."""
import importlib.util
import json
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bf_sync_source', ROOT / 'scripts/common/sync_windows_source.py')
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)


@pytest.fixture
def copies(tmp_path):
    source, target = tmp_path / 'linux', tmp_path / 'windows'
    source.mkdir()
    (source / 'version.json').write_text('{"version":"test"}')
    (source / 'app.cpp').write_text('original')
    return source, target


def initialize(copies):
    source, target = copies
    sync.synchronize(source, target, initialize=True)
    return source, target


def test_add_update_delete_and_ignore_local_build_files(copies):
    source, target = initialize(copies)
    for name in ['native/build-windows/cache.txt', 'release/app.txt', 'dist/setup.txt',
                 'config/windows-dev.local.json', 'native/CMakeUserPresets.json', 'license.dat']:
        path = target / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('Windows-local')
    (source / 'app.cpp').unlink()
    (source / 'new.cpp').write_text('new source')
    result = sync.synchronize(source, target)
    assert result['removed'] == ['app.cpp']
    assert not (target / 'app.cpp').exists()
    assert (target / 'new.cpp').read_text() == 'new source'
    assert (target / 'config/windows-dev.local.json').read_text() == 'Windows-local'
    assert (target / 'native/build-windows/cache.txt').read_text() == 'Windows-local'
    assert (target / 'license.dat').read_text() == 'Windows-local'
    assert sync.synchronize(source, target)['changed'] == []


@pytest.mark.parametrize('edit', ['modify', 'delete', 'add'])
def test_destination_changes_stop_sync_before_any_overwrite(copies, edit):
    source, target = initialize(copies)
    original_manifest = (target / sync.MANIFEST).read_bytes()
    (source / 'new.cpp').write_text('must not copy')
    if edit == 'modify':
        (target / 'app.cpp').write_text('local edit')
    elif edit == 'delete':
        (target / 'app.cpp').unlink()
    else:
        (target / 'extra.cpp').write_text('local addition')
    with pytest.raises(ValueError, match='local changes'):
        sync.synchronize(source, target)
    assert not (target / 'new.cpp').exists()
    assert (target / sync.MANIFEST).read_bytes() == original_manifest


def test_gitignored_files_are_not_copied(copies):
    source, target = copies
    subprocess.run(['git', '-C', str(source), 'init', '-q'], check=True)
    (source / '.gitignore').write_text('scratch/\n')
    (source / 'scratch').mkdir()
    (source / 'scratch/temp.txt').write_text('not source')
    sync.synchronize(source, target, initialize=True)
    assert not (target / 'scratch').exists()
    assert (target / 'app.cpp').exists()  # Includes ordinary untracked source.


def test_dry_run_never_changes_source_or_destination(copies):
    source, target = copies
    result = sync.synchronize(source, target, initialize=True, dry_run=True)
    assert result['dry_run'] and not target.exists()
    initialize(copies)
    before = (target / sync.MANIFEST).read_bytes()
    (source / 'app.cpp').write_text('new')
    result = sync.synchronize(source, target, dry_run=True)
    assert result['changed'] == ['app.cpp']
    assert (target / 'app.cpp').read_text() == 'original'
    assert (target / sync.MANIFEST).read_bytes() == before


def test_reverse_sync_from_build_mirror_is_rejected(copies):
    source, target = initialize(copies)
    with pytest.raises(ValueError, match='cannot be used as the development source'):
        sync.synchronize(target, source)


def test_unmanaged_existing_copy_is_never_adopted_implicitly(copies):
    source, target = copies
    target.mkdir()
    (target / 'version.json').write_text('existing candidate')
    with pytest.raises(ValueError, match='Unmanaged or non-empty'):
        sync.synchronize(source, target, initialize=True)
    assert (target / 'version.json').read_text() == 'existing candidate'


def test_other_source_cannot_overwrite_an_existing_mirror(copies, tmp_path):
    source, target = initialize(copies)
    other = tmp_path / 'old-checkout'
    other.mkdir()
    (other / 'version.json').write_text('{}')
    with pytest.raises(ValueError, match='different development source'):
        sync.synchronize(other, target)


def test_failed_copy_rolls_back_source_files_and_manifest(copies, monkeypatch):
    source, target = initialize(copies)
    before = (target / sync.MANIFEST).read_bytes()
    (source / 'app.cpp').write_text('updated')
    (source / 'new.cpp').write_text('new')
    original = sync.shutil.copy2

    def fail_once(src, dst, *args, **kwargs):
        if Path(dst) == target / 'new.cpp':
            raise OSError('injected write failure')
        return original(src, dst, *args, **kwargs)

    monkeypatch.setattr(sync.shutil, 'copy2', fail_once)
    with pytest.raises(OSError, match='injected'):
        sync.synchronize(source, target)
    assert (target / 'app.cpp').read_text() == 'original'
    assert not (target / 'new.cpp').exists()
    assert (target / sync.MANIFEST).read_bytes() == before
    assert not (target / sync.LOCK).exists()


def test_source_edit_during_staging_is_rejected(copies, monkeypatch):
    source, target = initialize(copies)
    (source / 'app.cpp').write_text('updated')
    original = sync.shutil.copy2

    def change_source(src, dst, *args, **kwargs):
        result = original(src, dst, *args, **kwargs)
        if Path(src) == source / 'app.cpp':
            (source / 'app.cpp').write_text('edited again')
        return result

    monkeypatch.setattr(sync.shutil, 'copy2', change_source)
    with pytest.raises(ValueError, match='Source changed during synchronization'):
        sync.synchronize(source, target)
    assert (target / 'app.cpp').read_text() == 'original'


def test_manifest_path_traversal_and_target_symlinks_are_rejected(copies, tmp_path):
    source, target = initialize(copies)
    state = json.loads((target / sync.MANIFEST).read_text())
    state['files']['../outside.txt'] = {'sha256': '0' * 64, 'size': 0}
    (target / sync.MANIFEST).write_text(json.dumps(state))
    with pytest.raises(ValueError, match='Invalid source path'):
        sync.synchronize(source, target)
    del state['files']['../outside.txt']
    (target / sync.MANIFEST).write_text(json.dumps(state))
    external = tmp_path / 'outside.txt'
    external.write_text('untouched')
    (target / 'app.cpp').unlink()
    (target / 'app.cpp').symlink_to(external)
    with pytest.raises(ValueError, match='symlink'):
        sync.synchronize(source, target)
    assert external.read_text() == 'untouched'


def test_nested_source_and_target_are_rejected(copies):
    source, _ = copies
    with pytest.raises(ValueError, match='separate directories'):
        sync.synchronize(source, source / 'mirror', initialize=True)


def test_existing_sync_lock_is_preserved(copies):
    source, target = initialize(copies)
    (target / sync.LOCK).write_text('another process')
    with pytest.raises(ValueError, match='already active'):
        sync.synchronize(source, target)
    assert (target / sync.LOCK).read_text() == 'another process'
