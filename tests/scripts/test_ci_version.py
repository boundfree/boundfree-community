"""Release mistakes must fail before compilation or remote publication."""
import importlib.util
import json
from pathlib import Path
import re
import sys
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/common'))
import version_sync
spec = importlib.util.spec_from_file_location('build_identity', ROOT / 'scripts/ci/build_identity.py')
identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(identity)
SHA = 'a' * 40

@pytest.mark.parametrize('value', ['0.2.3-dev..1', '0.2.3-dev.01', '0.2.3+', '01.2.3', '65536.0.0'])
def test_invalid_versions_rejected(value):
    with pytest.raises(SystemExit):
        version_sync.parse_version(value)


def test_builds_and_reruns_are_distinguishable():
    first = identity.resolve('0.2.2-dev', '158', '1', SHA)
    assert first == '0.2.2-dev.158.1+gaaaaaaaa'
    assert first != identity.resolve('0.2.2-dev', '158', '2', SHA)

@pytest.mark.parametrize('value,tag', [('0.2.2-dev', 'community-v0.2.2-dev'),
    ('0.2.2-beta.1', 'v0.2.2-beta.1'), ('0.2.2', 'community-v0.2.3'),
    ('0.2.2-beta', 'community-v0.2.2-beta'), ('0.2.2+dirty', 'community-v0.2.2+dirty')])
def test_release_rejects_dev_old_upstream_tags_and_mismatch(value, tag):
    with pytest.raises(ValueError):
        identity.resolve(value, '1', '1', SHA, release=True, tag=tag)

@pytest.mark.parametrize('value', ['0.2.2-beta.1', '0.2.2'])
def test_release_preserves_exact_version(value):
    assert identity.resolve(value, '1', '1', SHA, release=True, tag='community-v' + value) == value


def test_channel_mismatch_fails_before_writing():
    before = version_sync.VERSION_JSON.read_bytes()
    with pytest.raises(SystemExit):
        version_sync.set_version('0.2.2', 'dev')
    assert version_sync.VERSION_JSON.read_bytes() == before


@pytest.mark.parametrize('source', ['0.2.2-dev', '0.2.2-beta.1', '0.2.2'])
def test_ordinary_ci_never_labels_an_unsigned_package_stable(source):
    assert identity.resolve(source, '158', '1', SHA) == '0.2.2-dev.158.1+gaaaaaaaa'


def test_delivery_cycle_updates_all_five_mirrors(tmp_path, monkeypatch):
    for attribute, relative in [('VERSION_JSON', 'version.json'), ('VERSION_MD', 'version.md'),
                                ('README', 'README.md'), ('AGENTS', 'AGENTS.md'),
                                ('ISS', 'installer/BoundFreeEEG.iss')]:
        target = tmp_path / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((ROOT / relative).read_bytes())
        monkeypatch.setattr(version_sync, attribute, target)
    for value in ('0.2.2-beta.1', '0.2.2-beta.2', '0.2.2', '0.2.3-dev'):
        version_sync.set_version(value, None)
        version_sync.add_entry(value, 'chore', '测试交付阶段')
        assert version_sync.check_sync() == []
        readme_json = re.search(r'```json\s*(.*?)```', version_sync.README.read_text(encoding='utf-8'), re.S)
        assert readme_json, 'README must document the current version.json'
        assert json.loads(readme_json.group(1)) == version_sync.read_version_json()
    assert version_sync.bump('minor') == '0.3.0-dev'
    readme = version_sync.README.read_text(encoding='utf-8')
    version_sync.README.write_text(readme.replace('"release_channel": "dev"',
                                                '"release_channel": "beta"'), encoding='utf-8')
    assert 'README release_channel JSON block mismatch' in version_sync.check_sync()
