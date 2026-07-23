import importlib
import importlib.util
from pathlib import Path
from unittest import mock

import pytest


ROOT = Path(__file__).resolve().parents[2]
SIDECAAR = ROOT / "sidecar"
HAS_NEO = importlib.util.find_spec("neo") is not None

# 旧参考仓库样本目录；Intan RHD/RHS 样本若存在则用于真实读取测试。
INTAN_SAMPLE_DIR = ROOT.parent / "boundfree-eeg-upper-computer" / "samples"


def _find_intan_sample() -> Path | None:
    if not INTAN_SAMPLE_DIR.exists():
        return None
    for ext in ("*.rhd", "*.rhs"):
        hits = sorted(INTAN_SAMPLE_DIR.glob(ext))
        if hits:
            return hits[0]
    return None


INTAN_SAMPLE = _find_intan_sample()


def test_registry_returns_intan_reader_for_rhd():
    from boundfree_sidecar.readers.intan_reader import IntanReader
    from boundfree_sidecar.readers.registry import reader_for_path

    reader = reader_for_path(Path("foo.rhd"))
    assert isinstance(reader, IntanReader)
    assert reader.format_name == "Intan RHD"


def test_registry_returns_intan_reader_for_rhs():
    from boundfree_sidecar.readers.intan_reader import IntanReader
    from boundfree_sidecar.readers.registry import reader_for_path

    reader = reader_for_path(Path("foo.rhs"))
    assert isinstance(reader, IntanReader)
    assert reader.format_name == "Intan RHS"


def test_open_intan_without_neo_returns_optional_dependency_error(tmp_path):
    """neo 不可用时，open 必须返回 reader.optional_dependency_missing，而不是裸崩溃。"""
    from boundfree_sidecar import protocol
    from boundfree_sidecar.readers import intan_reader as intan_mod

    fake = tmp_path / "fake.rhd"
    fake.write_bytes(b"\x00" * 16)

    # 强制 neo 导入失败：在 protocol 已 import 的 sys.modules 里清掉 neo，并 patch import。
    with mock.patch.dict("sys.modules", {"neo": None}):
        with mock.patch.object(intan_mod, "_import_neo", side_effect=intan_mod.OptionalDependencyMissing("neo", "simulated import failure")):
            # protocol 模块在 import 时已绑定 readers，需重新加载以拿到 patch 后的引用？
            # 这里 readers.intan_reader.IntanReader._load_block 调用模块级 _import_neo，
            # patch object 已覆盖该引用，故无需 reload。
            response = protocol.handle_request(
                {"id": "no-neo", "method": "open", "params": {"path": str(fake)}}
            )

    assert response["id"] == "no-neo"
    assert response["ok"] is False
    assert response["error"]["code"] == "reader.optional_dependency_missing"
    assert response["error"]["params"]["dependency"] == "neo"
    assert response["error"]["params"]["path"] == str(fake)
    assert response["error"]["params"]["reason"]
    assert response["error"]["retryable"] is False


def test_open_intan_missing_file_returns_file_not_found():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request(
        {"id": "missing", "method": "open", "params": {"path": "/nonexistent/foo.rhd"}}
    )
    assert response["id"] == "missing"
    assert response["ok"] is False
    assert response["error"]["code"] == "file.not_found"


@pytest.mark.skipif(not HAS_NEO, reason="neo is not installed")
@pytest.mark.skipif(not INTAN_SAMPLE, reason="no Intan RHD/RHS sample available")
def test_intan_reader_opens_real_sample():
    from boundfree_sidecar.protocol import handle_request

    response = handle_request(
        {"id": "intan-open", "method": "open", "params": {"path": str(INTAN_SAMPLE)}}
    )
    assert response["ok"] is True, response
    result = response["result"]
    assert result["format"] in {"Intan RHD", "Intan RHS"}
    assert result["fs"] > 0
    assert result["n_channels"] > 0
    assert result["n_samples"] > 0
    assert result["duration"] > 0
    assert result["unit"] == "uV"


@pytest.mark.skipif(not HAS_NEO, reason="neo is not installed")
@pytest.mark.skipif(not INTAN_SAMPLE, reason="no Intan RHD/RHS sample available")
def test_intan_reader_get_slice_returns_uv_window():
    from boundfree_sidecar.protocol import handle_request

    opened = handle_request(
        {"id": "intan-open-2", "method": "open", "params": {"path": str(INTAN_SAMPLE)}}
    )
    handle = opened["result"]["handle"]

    response = handle_request(
        {"id": "intan-slice", "method": "get_slice", "params": {"handle": handle, "t0": 0.0, "t1": 0.01}}
    )
    assert response["ok"] is True, response
    assert response["result"]["shape"][0] == opened["result"]["n_channels"]
    assert response["result"]["shape"][1] > 0
    assert response["result"]["unit"] == "uV"
