"""Exercise release gates using real Git tags/history and a local GitHub CLI stub."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]

@pytest.fixture
def sandbox(tmp_path):
    if os.name == 'nt':
        pytest.skip('POSIX fake gh entry; the production release preflight runs on Ubuntu')
    subprocess.run(['git', 'init', '-q', str(tmp_path)], check=True)
    def git(*args):
        return subprocess.check_output(['git', '-C', str(tmp_path), *args], text=True).strip()
    git('config', 'user.email', 'release-test@example.invalid')
    git('config', 'user.name', 'Release gate test')
    bins = tmp_path / 'bin'
    bins.mkdir()
    gh = bins / 'gh'
    gh.write_text('#!' + sys.executable + '\n' + '''import os, sys
if '/issues/' in sys.argv[-1]:
    print('{"number": 2}')
elif os.environ.get('FAKE_RELEASE') == 'exists':
    print('{"id": 1}')
elif os.environ.get('FAKE_RELEASE') == 'error':
    print('HTTP 500', file=sys.stderr); sys.exit(1)
else:
    print('HTTP 404', file=sys.stderr); sys.exit(1)
''')
    gh.chmod(0o755)
    return tmp_path, git


def run_gate(sandbox, version='0.2.2-beta.1', *, annotated=True, on_master=True,
             tag=None, issue=None, accepted='false', release='absent', workflow_ref='refs/heads/master'):
    tmp, git = sandbox
    channel = 'beta' if '-beta' in version else 'stable'
    (tmp / 'version.json').write_text(json.dumps({'version': version, 'release_channel': channel}))
    git('add', 'version.json')
    git('commit', '-qm', 'release test')
    sha = git('rev-parse', 'HEAD')
    git('update-ref', 'refs/remotes/origin/master', sha)
    if not on_master:
        (tmp / 'new-code').write_text('outside master')
        git('add', 'new-code')
        git('commit', '-qm', 'unmerged')
    name = tag or 'community-v' + version
    git('tag', *(['-a', name, '-m', 'test'] if annotated else [name]))
    env = dict(os.environ, PATH=str(tmp / 'bin') + os.pathsep + os.environ['PATH'],
               RELEASE_TAG=name, GITHUB_REPOSITORY='owner/community', GITHUB_REF=workflow_ref,
               ACCEPTANCE_ISSUE=issue or 'https://github.com/owner/community/issues/2',
               ACCEPTED=accepted, FAKE_RELEASE=release, GITHUB_OUTPUT=str(tmp / 'output'))
    result = subprocess.run([sys.executable, str(ROOT / 'scripts/ci/release_preflight.py')],
                            cwd=tmp, env=env, capture_output=True, text=True)
    return result, tmp / 'output', sha


def test_beta_preflight_produces_exact_commit(sandbox):
    result, output, sha = run_gate(sandbox)
    assert result.returncode == 0, result.stderr
    assert output.read_text() == f'sha={sha}\nchannel=beta\n'

@pytest.mark.parametrize('options', [
    {'annotated': False}, {'on_master': False}, {'tag': 'community-v0.2.3-beta.1'},
    {'issue': 'https://github.com/another/community/issues/2'},
    {'version': '0.2.2'}, {'release': 'exists'}, {'release': 'error'},
    {'workflow_ref': 'refs/heads/feature/unreviewed'},
])
def test_preflight_blocks_unsafe_release(sandbox, options):
    result, output, _ = run_gate(sandbox, **options)
    assert result.returncode != 0
    assert not output.exists()


def test_stable_requires_explicit_acceptance(sandbox):
    result, output, sha = run_gate(sandbox, version='0.2.2', accepted='true')
    assert result.returncode == 0, result.stderr
    assert output.read_text() == f'sha={sha}\nchannel=stable\n'
