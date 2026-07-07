import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ERROR_CODES_PATH = ROOT / "contracts" / "error_codes.json"


def test_error_code_registry_is_valid_and_complete_for_i18n():
    registry = json.loads(ERROR_CODES_PATH.read_text(encoding="utf-8"))
    assert registry["schema_version"] == 1
    assert registry["error_codes"]

    code_pattern = re.compile(r"^[a-z0-9_]+(?:\.[a-z0-9_]+)+$")
    codes = [entry["code"] for entry in registry["error_codes"]]
    assert len(codes) == len(set(codes))
    assert all(code_pattern.fullmatch(code) for code in codes)

    zh_cn = ET.parse(ROOT / "native" / "i18n" / "bf_app_zh_CN.ts").getroot()
    keys = {message.findtext("source") for message in zh_cn.findall(".//message")}
    for code in codes:
        assert f"error.{code}" in keys


def test_error_code_registry_params_match_translation_placeholders():
    registry = json.loads(ERROR_CODES_PATH.read_text(encoding="utf-8"))
    zh_cn = ET.parse(ROOT / "native" / "i18n" / "bf_app_zh_CN.ts").getroot()
    translations = {
        message.findtext("source"): message.findtext("translation")
        for message in zh_cn.findall(".//message")
    }

    for entry in registry["error_codes"]:
        key = f'error.{entry["code"]}'
        placeholders = set(re.findall(r"\{([a-zA-Z_][a-zA-Z0-9_]*)\}", translations[key]))
        assert placeholders == set(entry["params"])


def test_m1b_error_codes_are_registered():
    registry = json.loads(ERROR_CODES_PATH.read_text(encoding="utf-8"))
    by_code = {entry["code"]: entry for entry in registry["error_codes"]}

    expected = {
        "protocol.invalid_params": ["field", "reason"],
        "file.not_found": ["path"],
        "reader.unsupported_format": ["path", "suffix"],
        "reader.session_not_found": ["handle"],
        "reader.slice_out_of_range": ["handle", "t0", "t1", "duration"],
        "license.missing": ["path"],
        "license.invalid_signature": ["reason"],
        "license.expired": ["expires_at"],
    }
    for code, params in expected.items():
        assert code in by_code
        assert by_code[code]["params"] == params
