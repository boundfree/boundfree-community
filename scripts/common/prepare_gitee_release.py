#!/usr/bin/env python3
"""Prepare verified Gitee attachments; never publish or rebuild a release."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile


PART_BYTES = 90_000_000  # Below Gitee's 100M single-attachment limit.


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            result.update(block)
    return result.hexdigest()


def inspect(folder, platform):
    info_name = f'build-info-{platform}.json'
    info = json.loads((folder / info_name).read_text(encoding='utf-8'))
    version = info['version']
    if (not re.fullmatch(r'\d+\.\d+\.\d+(?:-beta\.[1-9]\d*)?', version)
            or info.get('release_build') is not True
            or info.get('source_version') != version
            or info.get('tag') != 'community-v' + version
            or not re.fullmatch(r'[0-9a-f]{40}', info.get('git_commit', ''))
            or not re.fullmatch(r'https://github\.com/(?:boundfree/boundfree-community|Vsonic12138/boundfree-eeg-native)/actions/runs/[1-9]\d*',
                                info.get('run_url', ''))):
        raise ValueError('Only exact beta/stable Release artifacts are accepted')
    package = (f'BoundFreeEEG_Setup_{version}.exe' if platform == 'Windows'
               else f'BoundFreeEEG_{version}_ubuntu24.04-x86_64.tar.gz')
    checksums = {}
    for line in (folder / f'SHA256SUMS-{platform}.txt').read_text(encoding='utf-8').splitlines():
        sha, name = line.split('  ', 1)
        if name not in {package, info_name} or name in checksums or not re.fullmatch(r'[0-9a-f]{64}', sha):
            raise ValueError('Unexpected or duplicate source checksum entry')
        checksums[name] = sha
    if set(checksums) != {package, info_name}:
        raise ValueError('Source checksums must cover package and build info')
    for name, sha in checksums.items():
        path = folder / name
        if path.is_symlink() or not path.is_file() or digest(path) != sha:
            raise ValueError(f'Source SHA256 mismatch: {name}')
    if (folder / package).stat().st_size == 0:
        raise ValueError('Empty source package')
    return info, package, checksums[package]


def windows_restore(package, sha, parts):
    entries = ','.join("@{name='%s';sha='%s';size=%s}" % (p['name'], p['sha256'], p['bytes']) for p in parts)
    # All interpolated filenames are derived from the strict version/name grammar.
    command = (
        "$ErrorActionPreference='Stop'; $created=$false; "
        "function Hash([string]$path) { $file=[IO.File]::OpenRead($path); "
        "$algorithm=[Security.Cryptography.SHA256]::Create(); "
        "try { return [BitConverter]::ToString($algorithm.ComputeHash($file)).Replace('-','') } "
        "finally { $algorithm.Dispose(); $file.Dispose() } }; "
        f"$parts=@({entries}); $output='{package}'; $tmp=$output+'.partial'; "
        "try { foreach ($p in $parts) { "
        "if (([IO.FileInfo]::new($p.name)).Length -ne $p.size -or "
        "(Hash $p.name) -ne $p.sha) "
        "{ throw ('Part mismatch: '+$p.name) } }; "
        "if ([IO.File]::Exists($output)) { "
        f"if ((Hash $output) -eq '{sha}') "
        "{ [Console]::WriteLine('Already verified: '+$output); exit 0 }; throw 'Existing output differs; move it before retrying' }; "
        "$stream=[IO.File]::Open($tmp,[IO.FileMode]::CreateNew); $created=$true; "
        "try { foreach ($p in $parts) { $inputFile=[IO.File]::OpenRead($p.name); "
        "try { $inputFile.CopyTo($stream) } finally { $inputFile.Dispose() } } } finally { $stream.Dispose() }; "
        f"if ((Hash $tmp) -ne '{sha}') {{ throw 'Restored package SHA256 mismatch' }}; "
        "[IO.File]::Move($tmp,$output); [Console]::WriteLine('Verified: '+$output) "
        "} catch { if ($created) { [IO.File]::Delete($tmp) }; "
        "[Console]::Error.WriteLine($_.Exception.Message); exit 1 }"
    )
    if len(command) > 7500:
        raise ValueError('Windows restore command exceeds cmd size limit; use fewer parts')
    return ('@echo off\r\nsetlocal\r\npushd "%~dp0" || exit /b 1\r\n'
            f'"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoProfile -Command "{command}"\r\n'
            'set result=%errorlevel%\r\npopd\r\nexit /b %result%\r\n')


def linux_restore(package, sha, parts):
    part_checks = '\n'.join(f"{p['sha256']}  {p['name']}" for p in parts)
    names = ' '.join("'" + p['name'] + "'" for p in parts)
    return f'''#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"
output='{package}'
tmp="$output.partial"
sha256sum --check <<'PART_CHECKSUMS'
{part_checks}
PART_CHECKSUMS
if [ -e "$output" ]; then
    printf '%s  %s\\n' '{sha}' "$output" | sha256sum --check
    exit 0
fi
(set -C; : > "$tmp")
trap 'rm -f -- "$tmp"' 0
cat -- {names} > "$tmp"
printf '%s  %s\\n' '{sha}' "$tmp" | sha256sum --check
ln -- "$tmp" "$output"
printf 'Verified: %s\\n' "$output"
'''


def prepare(windows, linux, output, part_bytes=PART_BYTES):
    if not 1 <= part_bytes <= PART_BYTES:
        raise ValueError('Part size must be between 1 and 90000000 bytes')
    if output.exists():
        raise FileExistsError('Use a new output directory; existing files are never replaced')
    records = [(platform, folder, *inspect(folder, platform))
               for platform, folder in [('Windows', windows), ('Linux', linux)]]
    first = records[0][2]
    for _, _, info, _, _ in records:
        if any(info.get(key) != first.get(key) for key in ('version', 'git_commit', 'tag', 'run_url')):
            raise ValueError('Windows and Linux must come from the same Release build')
    plan = {'version': first['version'], 'tag': first['tag'], 'git_commit': first['git_commit'],
            'run_url': first.get('run_url'), 'platforms': {}}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.gitee-prepare-', dir=output.parent) as temporary:
        stage = Path(temporary) / 'attachments'
        stage.mkdir()
        for platform, folder, info, package, sha in records:
            target = stage / platform
            target.mkdir()
            parts = []
            with (folder / package).open('rb') as source:
                number = 0
                while block := source.read(part_bytes):
                    number += 1
                    part = target / f'{package}.part{number:03d}'
                    part.write_bytes(block)
                    parts.append({'name': part.name, 'bytes': len(block), 'sha256': hashlib.sha256(block).hexdigest()})
            restore_name = f'Restore-{platform}.' + ('cmd' if platform == 'Windows' else 'sh')
            restore = windows_restore(package, sha, parts) if platform == 'Windows' else linux_restore(package, sha, parts)
            (target / restore_name).write_bytes(restore.encode('ascii'))
            for name in (f'build-info-{platform}.json', f'SHA256SUMS-{platform}.txt'):
                shutil.copyfile(folder / name, target / name)
            checks = ''.join(f'{digest(path)}  {path.name}\n' for path in sorted(target.iterdir()))
            (target / f'SHA256SUMS-Gitee-{platform}.txt').write_bytes(checks.encode('ascii'))
            plan['platforms'][platform] = {'original': package, 'sha256': sha, 'parts': parts, 'restore': restore_name}
        (stage / 'mirror-plan.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
        stage.rename(output)
    return plan


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows', type=Path, required=True, help='Folder with Windows Release artifacts')
    parser.add_argument('--linux', type=Path, required=True, help='Folder with Linux Release artifacts')
    parser.add_argument('--output', type=Path, required=True, help='New directory outside the source tree')
    args = parser.parse_args()
    try:
        result = prepare(args.windows, args.linux, args.output)
    except (ValueError, OSError, KeyError) as error:
        parser.exit(1, f'Gitee preparation stopped: {error}\n')
    print(json.dumps(result, indent=2))
