#!/usr/bin/env python3
import re
import sys
import json
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
I18N_DIR = ROOT / "native" / "i18n"
NATIVE_SRC = ROOT / "native" / "src"
ERROR_CODES_PATH = ROOT / "contracts" / "error_codes.json"
LOCALES = ["zh_CN", "zh_TW", "en_US", "ja_JP", "ko_KR", "de_DE", "fr_FR", "es_ES", "pseudo"]
PLACEHOLDER_RE = re.compile(r"\{[a-zA-Z_][a-zA-Z0-9_]*\}")
CHINESE_RE = re.compile(r"[\u4e00-\u9fff]")


def load_error_codes() -> list[dict[str, object]]:
    registry = json.loads(ERROR_CODES_PATH.read_text(encoding="utf-8"))
    if registry.get("schema_version") != 1:
        raise AssertionError("contracts/error_codes.json schema_version must be 1")
    return list(registry["error_codes"])


def load_messages(locale: str) -> dict[str, str]:
    path = I18N_DIR / f"bf_app_{locale}.ts"
    if not path.exists():
        raise AssertionError(f"missing translation file: {path}")
    root = ET.parse(path).getroot()
    messages: dict[str, str] = {}
    for message in root.findall(".//message"):
        source = message.findtext("source")
        translation = message.findtext("translation")
        if not source:
            raise AssertionError(f"{path}: message without source")
        if translation is None or translation == "":
            raise AssertionError(f"{path}: empty translation for {source}")
        messages[source] = translation
    return messages


def check_keys_and_placeholders() -> None:
    baseline = load_messages("zh_CN")
    baseline_keys = set(baseline)
    error_codes = load_error_codes()
    for entry in error_codes:
        code = str(entry["code"])
        key = f"error.{code}"
        if key not in baseline_keys:
            raise AssertionError(f"missing error translation key: {key}")

    baseline_placeholders = {
        key: set(PLACEHOLDER_RE.findall(value)) for key, value in baseline.items()
    }
    for locale in LOCALES:
        messages = load_messages(locale)
        keys = set(messages)
        if keys != baseline_keys:
            missing = sorted(baseline_keys - keys)
            extra = sorted(keys - baseline_keys)
            raise AssertionError(f"{locale}: key mismatch missing={missing} extra={extra}")
        for key, value in messages.items():
            placeholders = set(PLACEHOLDER_RE.findall(value))
            if placeholders != baseline_placeholders[key]:
                raise AssertionError(
                    f"{locale}:{key}: placeholder mismatch "
                    f"expected={sorted(baseline_placeholders[key])} actual={sorted(placeholders)}"
                )

    for entry in error_codes:
        key = f"error.{entry['code']}"
        expected_params = {f"{{{param}}}" for param in entry["params"]}
        if entry["code"] == "reader.open_failed":
            if baseline_placeholders[key] != {"{reason}"}:
                raise AssertionError(
                    f"{key}: registry params mismatch "
                    f"expected=['{{reason}}'] actual={sorted(baseline_placeholders[key])}"
                )
            continue
        if baseline_placeholders[key] != expected_params:
            raise AssertionError(
                f"{key}: registry params mismatch "
                f"expected={sorted(expected_params)} actual={sorted(baseline_placeholders[key])}"
            )


def check_no_hardcoded_chinese_in_ui() -> None:
    for path in NATIVE_SRC.rglob("*"):
        if path.suffix not in {".cpp", ".h"}:
            continue
        text = path.read_text(encoding="utf-8")
        for lineno, line in enumerate(text.splitlines(), start=1):
            if CHINESE_RE.search(line):
                raise AssertionError(f"hardcoded Chinese text in {path}:{lineno}")


def main() -> int:
    try:
        check_keys_and_placeholders()
        check_no_hardcoded_chinese_in_ui()
    except AssertionError as exc:
        print(f"i18n check failed: {exc}", file=sys.stderr)
        return 1
    print("i18n check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
