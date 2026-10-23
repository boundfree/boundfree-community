#!/usr/bin/env python3
"""Resolve CI identity without committing generated versions back to Git."""
from __future__ import annotations
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'common'))
import version_sync as versions


def resolve(source: str, number: str, attempt: str, sha: str, *, release: bool = False, tag: str = '') -> str:
    parsed = versions.parse_version(source)
    if not re.fullmatch(r'[0-9a-f]{40}', sha):
        raise ValueError('a full Git commit SHA is required')
    for value in (number, attempt):
        if not re.fullmatch(r'[1-9][0-9]*', value):
            raise ValueError('build number and attempt must be positive integers')
    if release:
        if versions.default_channel(source) not in {'beta', 'stable'} or parsed.build:
            raise ValueError('Release supports only beta.N or stable versions without build metadata')
        if parsed.prerelease and not re.fullmatch(r'beta\.[1-9][0-9]*', parsed.prerelease):
            raise ValueError('use beta.N with a positive sequence number')
        if tag != 'community-v' + source:
            raise ValueError('tag must exactly match community-v<version.json version>')
        return source
    if versions.default_channel(source) == 'dev':
        return f'{parsed.major}.{parsed.minor}.{parsed.patch}-dev.{number}.{attempt}+g{sha[:8]}'
    return source


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--number', required=True)
    p.add_argument('--attempt', required=True)
    p.add_argument('--release', action='store_true')
    p.add_argument('--tag', default='')
    args = p.parse_args()
    failures = versions.check_sync()
    if failures:
        raise SystemExit('\n'.join(failures))
    sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    source = versions.current_version()
    version = resolve(source, args.number, args.attempt, sha, release=args.release, tag=args.tag)
    if version != source:
        versions.set_version(version, versions.default_channel(version))
        versions.add_entry(version, 'chore', 'CI 临时构建版本（不回写仓库）')
    info = {'source_version': source, 'version': version, 'git_commit': sha,
            'run_number': args.number, 'run_attempt': args.attempt, 'tag': args.tag,
            'release_build': args.release}
    Path('build-info.json').write_text(json.dumps(info, indent=2) + '\n')
    print(version)

if __name__ == '__main__':
    main()
