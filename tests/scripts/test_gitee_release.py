"""Gitee transport must restore the exact official package or stop."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('prepare_gitee_release', ROOT / 'scripts/common/prepare_gitee_release.py')
mirror = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mirror)


@pytest.fixture
def artifacts(tmp_path):
    folders = {}
    for platform in ('Windows', 'Linux'):
        folder = tmp_path / platform
        folder.mkdir()
        info = {'source_version': '0.2.2-beta.1', 'version': '0.2.2-beta.1',
                'git_commit': 'a' * 40, 'release_build': True, 'tag': 'community-v0.2.2-beta.1',
                'run_url': 'https://github.com/Vsonic12138/boundfree-eeg-native/actions/runs/1'}
        (folder / f'build-info-{platform}.json').write_text(json.dumps(info), encoding='utf-8')
        name = ('BoundFreeEEG_Setup_0.2.2-beta.1.exe' if platform == 'Windows'
                else 'BoundFreeEEG_0.2.2-beta.1_ubuntu24.04-x86_64.tar.gz')
        (folder / name).write_bytes(bytes(range(256)) * 73 + platform.encode())
        folders[platform] = folder
        write_checksums(folder, platform)
    return folders


def write_checksums(folder, platform):
    files = sorted(p for p in folder.iterdir() if not p.name.startswith('SHA256SUMS'))
    checksums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files)
    (folder / f'SHA256SUMS-{platform}.txt').write_text(checksums, encoding='utf-8')


def prepare(artifacts, output):
    return mirror.prepare(artifacts['Windows'], artifacts['Linux'], output, part_bytes=8192)


@pytest.mark.parametrize('platform', ['Windows', 'Linux'])
def test_all_parts_and_source_identity_match(artifacts, tmp_path, platform):
    output = tmp_path / 'mirror with spaces'
    plan = prepare(artifacts, output)
    entry = plan['platforms'][platform]
    folder = output / platform
    restored = b''.join((folder / p['name']).read_bytes() for p in entry['parts'])
    assert restored == (artifacts[platform] / entry['original']).read_bytes()
    assert all(p['bytes'] <= 8192 for p in entry['parts'])
    for line in (folder / f'SHA256SUMS-Gitee-{platform}.txt').read_text().splitlines():
        sha, name = line.split('  ', 1)
        assert hashlib.sha256((folder / name).read_bytes()).hexdigest() == sha
    assert plan['git_commit'] == 'a' * 40


@pytest.mark.parametrize('problem', ['corrupt', 'mixed_build', 'dev', 'missing_checksum'])
def test_invalid_sources_never_create_output(artifacts, tmp_path, problem):
    folder = artifacts['Windows']
    info_path = folder / 'build-info-Windows.json'
    info = json.loads(info_path.read_text())
    if problem == 'corrupt':
        next(folder.glob('*.exe')).write_bytes(b'changed after signing')
    elif problem == 'missing_checksum':
        (folder / 'SHA256SUMS-Windows.txt').write_text('')
    else:
        if problem == 'mixed_build':
            info['git_commit'] = 'b' * 40
        else:
            info['release_build'] = False
        info_path.write_text(json.dumps(info), encoding='utf-8')
        write_checksums(folder, 'Windows')
    output = tmp_path / 'invalid'
    with pytest.raises(ValueError):
        prepare(artifacts, output)
    assert not output.exists()


def test_existing_directory_is_preserved(artifacts, tmp_path):
    output = tmp_path / 'existing'
    output.mkdir()
    marker = output / 'user-file'
    marker.write_bytes(b'keep')
    with pytest.raises(FileExistsError):
        prepare(artifacts, output)
    assert marker.read_bytes() == b'keep'


@pytest.mark.skipif(os.name == 'nt', reason='Linux restoration uses the target system sh/sha256sum')
@pytest.mark.parametrize('problem', ['none', 'corrupt', 'missing', 'existing_output', 'existing_partial'])
def test_linux_restore_checks_before_installation(artifacts, tmp_path, problem):
    output = tmp_path / 'mirror with spaces'
    entry = prepare(artifacts, output)['platforms']['Linux']
    folder = output / 'Linux'
    part = folder / entry['parts'][-1]['name']
    package = folder / entry['original']
    partial = package.with_name(package.name + '.partial')
    if problem == 'corrupt':
        part.write_bytes(b'bad')
    elif problem == 'missing':
        part.unlink()
    elif problem == 'existing_output':
        package.write_bytes(b'keep user output')
    elif problem == 'existing_partial':
        partial.write_bytes(b'keep user partial')
    result = subprocess.run(['sh', str(folder / entry['restore'])], cwd=tmp_path, capture_output=True, text=True)
    assert (result.returncode == 0) == (problem == 'none'), result.stdout + result.stderr
    if problem == 'none':
        assert package.read_bytes() == (artifacts['Linux'] / entry['original']).read_bytes()
        assert not partial.exists()
    elif problem == 'existing_output':
        assert package.read_bytes() == b'keep user output'
    else:
        assert not package.exists()
    if problem == 'existing_partial':
        assert partial.read_bytes() == b'keep user partial'


@pytest.mark.skipif(os.name != 'nt', reason='Windows restoration runs cmd and Windows PowerShell on Windows CI')
@pytest.mark.parametrize('problem', ['none', 'corrupt', 'missing', 'existing_output', 'existing_partial'])
def test_windows_restore_checks_before_installation(artifacts, tmp_path, problem):
    output = tmp_path / 'mirror with spaces'
    entry = prepare(artifacts, output)['platforms']['Windows']
    folder = output / 'Windows'
    package = folder / entry['original']
    partial = package.with_name(package.name + '.partial')
    part = folder / entry['parts'][-1]['name']
    if problem == 'corrupt':
        part.write_bytes(b'bad')
    elif problem == 'missing':
        part.unlink()
    elif problem == 'existing_output':
        package.write_bytes(b'keep user output')
    elif problem == 'existing_partial':
        partial.write_bytes(b'keep user partial')
    environment = os.environ.copy()
    environment['PSModulePath'] = str(tmp_path / 'no optional PowerShell modules')
    result = subprocess.run(['cmd.exe', '/d', '/c', entry['restore']], cwd=folder,
                            env=environment, capture_output=True, text=True)
    assert (result.returncode == 0) == (problem == 'none'), result.stdout + result.stderr
    if problem == 'none':
        assert package.read_bytes() == (artifacts['Windows'] / entry['original']).read_bytes()
        assert not partial.exists()
    elif problem == 'existing_output':
        assert package.read_bytes() == b'keep user output'
    else:
        assert not package.exists()
    if problem == 'existing_partial':
        assert partial.read_bytes() == b'keep user partial'
