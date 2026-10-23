#!/usr/bin/env python3
"""Synchronize BoundFree EEG Native product version metadata.

version.json is the only version truth. version.md is the human-readable
delivery-stage ledger; ordinary commits are recorded by Git.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VERSION_JSON = ROOT / "version.json"
VERSION_MD = ROOT / "version.md"
README = ROOT / "README.md"
AGENTS = ROOT / "AGENTS.md"
ISS = ROOT / "installer" / "BoundFreeEEG.iss"

VERSION_RE = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?$")
CHANNELS = {"dev", "alpha", "beta", "rc", "stable"}
TYPES = ("feat", "fix", "docs", "test", "chore", "spike", "refactor", "perf")


@dataclass(frozen=True)
class ParsedVersion:
    major: int
    minor: int
    patch: int
    prerelease: str | None = None
    build: str | None = None

    def render(self) -> str:
        value = f"{self.major}.{self.minor}.{self.patch}"
        if self.prerelease:
            value += f"-{self.prerelease}"
        if self.build:
            value += f"+{self.build}"
        return value


def parse_version(value: str) -> ParsedVersion:
    match = VERSION_RE.fullmatch(value)
    if not match:
        raise SystemExit(f"invalid SemVer: {value}")
    for group in (match.group(4), match.group(5)):
        if group and any(not part for part in group.split(".")):
            raise SystemExit(f"invalid SemVer: {value}")
    if match.group(4) and any(part.isdigit() and len(part) > 1 and part[0] == "0"
                              for part in match.group(4).split(".")):
        raise SystemExit(f"invalid SemVer: {value}")
    if any(int(match.group(i)) > 65535 for i in (1, 2, 3)):
        raise SystemExit("version components exceed Windows FileVersion limit (65535)")
    return ParsedVersion(int(match.group(1)), int(match.group(2)), int(match.group(3)), match.group(4), match.group(5))


def default_channel(version: str) -> str:
    parsed = parse_version(version)
    if parsed.prerelease:
        prefix = parsed.prerelease.split(".", 1)[0]
        return prefix if prefix in {"dev", "alpha", "beta", "rc"} else "dev"
    return "stable"


def read_version_json() -> dict[str, object]:
    return json.loads(VERSION_JSON.read_text(encoding="utf-8"))


def write_version_json(data: dict[str, object]) -> None:
    VERSION_JSON.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def current_version() -> str:
    version = str(read_version_json().get("version", ""))
    parse_version(version)
    return version


def set_version(version: str, channel: str | None) -> None:
    parse_version(version)
    chosen_channel = channel or default_channel(version)
    if chosen_channel not in CHANNELS:
        raise SystemExit(f"invalid release_channel: {chosen_channel}")
    if chosen_channel != default_channel(version):
        raise SystemExit(f"channel {chosen_channel} does not match version {version}")
    data = read_version_json()
    data["version"] = version
    data["release_channel"] = chosen_channel
    write_version_json(data)
    update_readme_version(version)
    update_agents_version(version)
    update_installer_version(version)
    update_version_md_current(version)
    print(f"version updated to {version} channel={chosen_channel}")


def bump(kind: str) -> str:
    parsed = parse_version(current_version())
    if kind == "patch":
        return ParsedVersion(parsed.major, parsed.minor, parsed.patch + 1, "dev").render()
    if kind == "minor":
        return ParsedVersion(parsed.major, parsed.minor + 1, 0, "dev").render()
    if kind == "major":
        return ParsedVersion(parsed.major + 1, 0, 0, "dev").render()
    raise SystemExit(f"unsupported bump kind: {kind}")


def update_readme_version(version: str) -> None:
    text = README.read_text(encoding="utf-8")
    text = re.sub(r'"version": "[^"]+"', f'"version": "{version}"', text, count=1)
    text = re.sub(r"产品版本 `[^`]+`", f"产品版本 `{version}`", text, count=1)
    README.write_text(text, encoding="utf-8")


def update_agents_version(version: str) -> None:
    text = AGENTS.read_text(encoding="utf-8")
    text = re.sub(r"当前版本为 `[^`]+`", f"当前版本为 `{version}`", text, count=1)
    text = re.sub(r"产品版本当前为 `[^`]+`", f"产品版本当前为 `{version}`", text, count=1)
    AGENTS.write_text(text, encoding="utf-8")


def update_installer_version(version: str) -> None:
    if not ISS.exists():
        raise SystemExit("installer/BoundFreeEEG.iss missing")
    text = ISS.read_text(encoding="utf-8")
    updated, count = re.subn(
        r'(#define\s+BoundFreeVersion\s+")[^"]+("\s*)',
        rf'\g<1>{version}\g<2>',
        text,
        count=1,
    )
    if count != 1:
        raise SystemExit("installer/BoundFreeEEG.iss missing BoundFreeVersion fallback")
    ISS.write_text(updated, encoding="utf-8")


def update_version_md_current(version: str) -> None:
    if not VERSION_MD.exists():
        raise SystemExit("version.md missing")
    text = VERSION_MD.read_text(encoding="utf-8")
    text = re.sub(r"^当前版本：.*$", f"当前版本：{version}", text, count=1, flags=re.MULTILINE)
    VERSION_MD.write_text(text, encoding="utf-8")


def add_entry(version: str, typ: str, summary: str, details: str = "") -> None:
    if not VERSION_MD.exists():
        raise SystemExit("version.md missing")
    text = VERSION_MD.read_text(encoding="utf-8")
    marker = "---\n\n"
    if marker not in text:
        raise SystemExit("version.md missing entry marker")
    body = details.strip()
    body_block = f"```text\n{body}\n```" if body else "无"
    today = date.today().isoformat()
    entry = (
        f"{typ}: {summary}\n\n"
        f"版本：{version}\n"
        f"日期：{today}\n\n"
        f"作者：BigYellow12138 <1373912749@qq.com>\n\n"
        f"类型：{typ}\n\n"
        f"提交标题：\n{typ}: {summary}\n\n"
        f"提交正文：\n{body_block}\n\n"
        f"---\n\n"
    )
    head, tail = text.split(marker, 1)
    VERSION_MD.write_text(head + marker + entry + tail, encoding="utf-8")


def check_sync() -> list[str]:
    failures: list[str] = []
    data = read_version_json()
    version = str(data.get("version", ""))
    channel = str(data.get("release_channel", ""))
    try:
        parse_version(version)
    except SystemExit as exc:
        failures.append(str(exc))
    if channel not in CHANNELS:
        failures.append(f"invalid release_channel: {channel}")
    expected_channel = default_channel(version)
    if channel != expected_channel:
        failures.append(f"release_channel={channel} does not match version {version} (expected {expected_channel})")
    if VERSION_MD.exists():
        vm = VERSION_MD.read_text(encoding="utf-8")
        if f"当前版本：{version}" not in vm:
            failures.append("version.md current version mismatch")
        if f"版本：{version}" not in vm:
            failures.append("version.md lacks entry for current version")
        if "提交标题：" not in vm or "提交正文：" not in vm:
            failures.append("version.md lacks normalized title/body sections")
    else:
        failures.append("version.md missing")
    readme = README.read_text(encoding="utf-8")
    if f'"version": "{version}"' not in readme:
        failures.append("README version JSON block mismatch")
    if f"产品版本 `{version}`" not in readme:
        failures.append("README status version mismatch")
    agents = AGENTS.read_text(encoding="utf-8")
    if f"当前版本为 `{version}`" not in agents:
        failures.append("AGENTS current version mismatch")
    if not ISS.exists():
        failures.append("installer/BoundFreeEEG.iss missing")
    elif not re.search(rf'#define\s+BoundFreeVersion\s+"{re.escape(version)}"', ISS.read_text(encoding="utf-8")):
        failures.append("installer/BoundFreeEEG.iss fallback version mismatch")
    return failures


def read_details(args: argparse.Namespace) -> str:
    return Path(args.message_file).read_text(encoding="utf-8") if args.message_file else args.details


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Synchronize BoundFree EEG Native version metadata.")
    sub = parser.add_subparsers(dest="cmd", required=True)
    next_p = sub.add_parser("next", help="bump version and add a version.md entry")
    next_p.add_argument("kind", nargs="?", choices=("patch", "minor", "major"), default="patch")
    next_p.add_argument("--type", required=True, choices=TYPES)
    next_p.add_argument("--summary", required=True)
    next_p.add_argument("--details", default="", help="full version.md commit body; keep git commit subject short")
    next_p.add_argument("--message-file", help="read full version.md commit body from a UTF-8 file")
    set_p = sub.add_parser("set", help="set exact version")
    set_p.add_argument("version")
    set_p.add_argument("--channel", choices=sorted(CHANNELS))
    set_p.add_argument("--type", choices=TYPES)
    set_p.add_argument("--summary")
    set_p.add_argument("--details", default="", help="full version.md commit body; keep git commit subject short")
    set_p.add_argument("--message-file", help="read full version.md commit body from a UTF-8 file")
    sub.add_parser("check", help="check version metadata consistency")
    args = parser.parse_args(argv)

    if args.cmd == "next":
        version = bump(args.kind)
        set_version(version, "dev")
        add_entry(version, args.type, args.summary, read_details(args))
        return 0
    if args.cmd == "set":
        set_version(args.version, args.channel)
        if args.type or args.summary or args.details or args.message_file:
            if not args.type or not args.summary:
                raise SystemExit("--type and --summary are required when adding a version.md entry")
            add_entry(args.version, args.type, args.summary, read_details(args))
        return 0
    if args.cmd == "check":
        failures = check_sync()
        if failures:
            for failure in failures:
                print(f"FAIL {failure}")
            return 1
        print(f"version sync ok: {current_version()} channel={read_version_json()['release_channel']}")
        return 0
    raise SystemExit(f"unknown command: {args.cmd}")


if __name__ == "__main__":
    sys.exit(main())
