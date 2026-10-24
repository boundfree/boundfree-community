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
    gh.write_text('#!' + sys.executable + '\n' + '''import json, os, sys
if '/issues/' in sys.argv[-1]:
    print('{"number": 2}')
elif sys.argv[-1] == 'repos/owner/community':
    mode = os.environ.get('FAKE_PUSH', 'true')
    print(json.dumps({} if mode == 'missing' else {'permissions': {'push': mode == 'true'}}))
elif (sys.argv[-1] == 'repos/owner/community/releases?per_page=100'
      and '--paginate' in sys.argv and '--slurp' in sys.argv):
    mode = os.environ.get('FAKE_RELEASE')
    tag = os.environ['RELEASE_TAG']
    if mode in ('exists', 'draft'):
        print(json.dumps([[{'tag_name': tag, 'draft': mode == 'draft'}]]))
    elif mode == 'draft_later_page':
        print(json.dumps([[{'tag_name': 'community-v0.1.0', 'draft': False}],
                          [{'tag_name': tag, 'draft': True}]]))
    elif mode == 'unrelated':
        print(json.dumps([[{'tag_name': 'community-v0.1.0', 'draft': True}]]))
    elif mode in ('error', 'not_found'):
        print('HTTP 500' if mode == 'error' else 'HTTP 404', file=sys.stderr); sys.exit(1)
    elif mode == 'invalid':
        print('{"message": "unexpected response"}')
    else:
        print('[[]]')
else:
    print('Unexpected GitHub CLI invocation', file=sys.stderr); sys.exit(1)
''')
    gh.chmod(0o755)
    return tmp_path, git


def run_gate(sandbox, version='0.2.2-beta.1', *, annotated=True, on_master=True,
             tag=None, issue=None, accepted='false', release='absent', workflow_ref='refs/heads/master', push_access='true'):
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
               ACCEPTED=accepted, FAKE_RELEASE=release, FAKE_PUSH=push_access, GITHUB_OUTPUT=str(tmp / 'output'))
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
    {'release': 'not_found'}, {'release': 'invalid'},
    {'workflow_ref': 'refs/heads/feature/unreviewed'},
])
def test_preflight_blocks_unsafe_release(sandbox, options):
    result, output, _ = run_gate(sandbox, **options)
    assert result.returncode != 0
    assert not output.exists()


@pytest.mark.parametrize('release', ['draft', 'draft_later_page'])
def test_existing_draft_blocks_before_build_output(sandbox, release):
    result, output, _ = run_gate(sandbox, release=release)
    assert result.returncode != 0
    assert 'Release already exists' in result.stderr
    assert not output.exists()


def test_other_tags_do_not_block_new_release(sandbox):
    result, output, sha = run_gate(sandbox, release='unrelated')
    assert result.returncode == 0, result.stderr
    assert output.read_text() == f'sha={sha}\nchannel=beta\n'


@pytest.mark.parametrize('push_access', ['false', 'missing'])
def test_draft_invisible_credentials_fail_closed(sandbox, push_access):
    result, output, _ = run_gate(sandbox, release='absent', push_access=push_access)
    assert result.returncode != 0
    assert 'push access to see draft releases' in result.stderr
    assert not output.exists()


def test_stable_requires_explicit_acceptance(sandbox):
    result, output, sha = run_gate(sandbox, version='0.2.2', accepted='true')
    assert result.returncode == 0, result.stderr
    assert output.read_text() == f'sha={sha}\nchannel=stable\n'
