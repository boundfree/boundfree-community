#!/usr/bin/env python3
"""Fail closed before expensive release builds; never create or overwrite tags."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_identity import resolve

def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()

if os.environ.get('GITHUB_REF', 'refs/heads/master') != 'refs/heads/master':
    raise SystemExit('Run Community Release from the master workflow branch')
tag = os.environ['RELEASE_TAG']
if not re.fullmatch(r'community-v\d+\.\d+\.\d+(?:-beta\.[1-9][0-9]*)?', tag):
    raise SystemExit('Only community-vX.Y.Z[-beta.N] release tags are allowed')
if git('cat-file', '-t', 'refs/tags/' + tag) != 'tag':
    raise SystemExit('Use an annotated tag: git tag -a ...')
sha = git('rev-parse', tag + '^{commit}')
subprocess.run(['git', 'merge-base', '--is-ancestor', sha, 'origin/master'], check=True)
data = json.loads(git('show', f'{sha}:version.json'))
resolve(data['version'], '1', '1', sha, release=True, tag=tag)
if data['release_channel'] not in {'beta', 'stable'}:
    raise SystemExit('Invalid release channel')
repo = os.environ['GITHUB_REPOSITORY']
url = os.environ['ACCEPTANCE_ISSUE']
match = re.fullmatch(r'https://github.com/' + re.escape(repo) + r'/issues/([1-9][0-9]*)', url)
if not match:
    raise SystemExit('Acceptance Issue must belong to this repository')
issue = json.loads(subprocess.check_output(['gh', 'api', f'repos/{repo}/issues/{match.group(1)}'], text=True))
if 'pull_request' in issue:
    raise SystemExit('Acceptance must be an Issue, not a pull request')
if data['release_channel'] == 'stable' and os.environ.get('ACCEPTED') != 'true':
    raise SystemExit('Stable acceptance must be explicitly confirmed')
# A 404 means absent; any authentication/server error must also stop the run.
check = subprocess.run(['gh', 'api', f'repos/{repo}/releases/tags/{tag}'], capture_output=True, text=True)
if check.returncode == 0:
    raise SystemExit('Release already exists; use a new version (including for drafts)')
if 'HTTP 404' not in check.stderr:
    raise SystemExit(check.stderr)
with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
    output.write(f'sha={sha}\nchannel={data["release_channel"]}\n')
