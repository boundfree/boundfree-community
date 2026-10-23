"""Release mistakes must fail before compilation or remote publication."""
import importlib.util
from pathlib import Path
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
