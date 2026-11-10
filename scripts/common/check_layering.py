#!/usr/bin/env python3
"""静态分层检查。

规则来源见 docs/architecture-design.md 的“分层与依赖规则”，落地要求见
docs/restructure-plan.md A1。检查两件事：

1. ``native/src`` 下源文件的 ``#include "..."`` 是否越过分层。带目录前缀的写法
   按 ``native/src`` 解析，不带前缀的写法先按所在目录解析（``../x.h`` 同理），
   因此 ``acquisition/protocol`` 里的 ``#include "codec.h"`` 视为引用自己。
2. ``native/src/ui`` 下是否出现 ``dsp::`` 函数调用。界面不实现算法，只允许读取
   ``dsp::eegBands()`` 这类常量定义；类型引用和类型构造不算函数调用。

注释与字符串字面量内的内容不算引用。现存违规逐条登记在 ``INCLUDE_EXCEPTIONS``
与 ``DSP_CALL_EXCEPTIONS`` 里，必须写明原因；条目没有命中任何违规时同样报错，
避免问题解决后留下无用白名单。

退出码 0 表示全部通过，1 表示存在违规或失效白名单。仓库根目录运行：
``python3 scripts/common/check_layering.py``。
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from fnmatch import fnmatch
from pathlib import Path, PurePosixPath

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SRC_ROOT = REPO_ROOT / "native" / "src"

SOURCE_SUFFIXES = frozenset({".h", ".hpp", ".cpp", ".cc"})
INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.MULTILINE)
DSP_CALL_RE = re.compile(r"\b(?:bf::)?dsp::(\w+)\s*\(")
# dsp 命名空间里的类型声明与别名；用于把 `dsp::Spectrum(...)` 这类类型构造和函数调用区分开。
DSP_TYPE_DECL_RE = re.compile(r"\b(?:enum\s+class|struct|class|enum)\s+(\w+)")
DSP_TYPE_ALIAS_RE = re.compile(r"^[ \t]*using\s+(\w+)\s*=", re.MULTILINE)

# acquisition 的基础子目录与协调子目录分属两层，见分层规则表。
ACQUISITION_FOUNDATION_DIRS = frozenset(
    {"adapters", "buffer", "model", "protocol", "session", "transport"}
)
ACQUISITION_COORDINATION_DIRS = frozenset({"control", "sources", "recording"})

FOUNDATION = "acquisition/foundation"
COORDINATION = "acquisition/coordination"

TOP_LEVEL_DIRS = frozenset(
    {"core", "dsp", "render", "ipc", "license", "i18n", "acquisition", "controller", "ui", "app"}
)

ALL_GROUPS = frozenset(
    {"core", "dsp", "render", "ipc", "license", "i18n", FOUNDATION, COORDINATION, "controller", "ui", "app"}
)

# acquisition 协调子目录在第 2 层，controller 与它同层，ui 再上一层。
_COORDINATION_ALLOWED = ALL_GROUPS - {"controller", "ui", "app"}
_CONTROLLER_ALLOWED = ALL_GROUPS - {"ui", "app"}
_UI_ALLOWED = ALL_GROUPS - {"app"}

# 每个分组允许引用的分组，含自身。core 与 dsp 是第 0 层，互不引用。
ALLOWED_GROUPS = {
    "core": frozenset({"core"}),
    "dsp": frozenset({"dsp"}),
    "render": frozenset({"core", "dsp", "render"}),
    "ipc": frozenset({"core", "dsp", "ipc"}),
    "license": frozenset({"core", "dsp", "license"}),
    "i18n": frozenset({"core", "dsp", "i18n"}),
    FOUNDATION: frozenset({"core", "dsp", FOUNDATION}),
    COORDINATION: _COORDINATION_ALLOWED,
    "controller": _CONTROLLER_ALLOWED,
    "ui": _UI_ALLOWED,
    "app": ALL_GROUPS,
}


@dataclass(frozen=True)
class IncludeException:
    """登记一条现存的分层例外。source 与 include 都按 fnmatch 匹配。"""

    source: str
    include: str
    reason: str


@dataclass(frozen=True)
class DspCallException:
    """登记一条 ui 里现存的 dsp 函数调用。source 按 fnmatch 匹配。"""

    source: str
    function: str
    reason: str


INCLUDE_EXCEPTIONS = (
    IncludeException(
        source="render/waveform_slice.h",
        include="ipc/sidecar_client.h",
        reason="waveformSliceFromSidecar 转换函数只为两个边车类型留在 render 头文件里；"
        "A3 把它搬到 controller/sidecar_slice 后删除本条",
    ),
    IncludeException(
        source="acquisition/control/*_test.cpp",
        include="ui/*",
        reason="这两个原生测试直接构造 device_workspace / waveform_widget / plot_style，"
        "测试文件与源码同目录（A9 不搬迁），因此引用了上一层 ui",
    ),
)

DSP_CALL_EXCEPTIONS = (
    DspCallException(
        source="ui/main_window.cpp",
        function="bandPower",
        reason="分析页按频段现算功率；A2 第 1 步移到 controller 的 AnalysisResult.bandPowers 后删除本条",
    ),
    DspCallException(
        source="ui/eeg_separation_workspace.cpp",
        function="computeWelchPsd",
        reason="分离工作区现算成分频谱；A2 第 2 步提取为 dsp::* 纯函数后删除本条",
    ),
)

# ui 里允许调用的 dsp 函数：读取常量定义，不做数值计算。
ALLOWED_DSP_CALLS = frozenset({"eegBands"})


def strip_comments(text: str) -> str:
    """去掉 // 与 /* */ 注释，保留换行以便行号不变；字符串与原始字符串原样保留。"""
    out: list[str] = []
    i = 0
    length = len(text)
    while i < length:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < length else ""
        if ch == "/" and nxt == "/":
            while i < length and text[i] != "\n":
                i += 1
        elif ch == "/" and nxt == "*":
            i += 2
            while i < length and not (text[i] == "*" and i + 1 < length and text[i + 1] == "/"):
                if text[i] == "\n":
                    out.append("\n")
                i += 1
            i += 2
        elif ch == '"' and _is_raw_string_start(out):
            i = _skip_raw_string(text, i, out)
        elif ch in "\"'":
            quote = ch
            out.append(ch)
            i += 1
            while i < length:
                out.append(text[i])
                if text[i] == "\\" and i + 1 < length:
                    out.append(text[i + 1])
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def _is_raw_string_start(emitted: list[str]) -> bool:
    return len(emitted) >= 1 and emitted[-1] == "R"


def _skip_raw_string(text: str, start: int, out: list[str]) -> int:
    """复制 R"delim( ... )delim" 整段，返回结束位置。"""
    delimiter_end = text.find("(", start + 1)
    if delimiter_end < 0:
        return start + 1
    delimiter = text[start + 1 : delimiter_end]
    closing = ")" + delimiter + '"'
    end = text.find(closing, delimiter_end + 1)
    if end < 0:
        end = len(text)
    else:
        end += len(closing)
    out.append(text[start:end])
    return end


def mask_literals(text: str) -> str:
    """把字符串与字符字面量的内容换成空格，保留引号与换行。

    调用前应先 strip_comments。用于避免把字面量里的 ``dsp::foo(`` 误判为函数调用。
    """
    out: list[str] = []
    i = 0
    length = len(text)
    while i < length:
        ch = text[i]
        if ch == '"' and _is_raw_string_start(out):
            delimiter_end = text.find("(", i + 1)
            closing = ')' + text[i + 1 : delimiter_end] + '"' if delimiter_end >= 0 else None
            end = text.find(closing, delimiter_end + 1) if closing else -1
            end = end + len(closing) if end >= 0 else length
            out.append("".join("\n" if c == "\n" else " " for c in text[i:end]))
            i = end
        elif ch in "\"'":
            quote = ch
            out.append(ch)
            i += 1
            while i < length and text[i] != quote:
                out.append("\n" if text[i] == "\n" else " ")
                if text[i] == "\\" and i + 1 < length:
                    out.append(" ")
                    i += 2
                    continue
                i += 1
            if i < length:
                out.append(quote)
                i += 1
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def iter_sources(src_root: Path):
    """按路径顺序返回 src_root 下的源文件。"""
    for path in sorted(src_root.rglob("*")):
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            yield path


def group_for(rel_path: PurePosixPath) -> str | None:
    """返回路径所属分组；未登记的分层目录返回 None。"""
    parts = rel_path.parts
    if not parts:
        return None
    top = parts[0]
    if top == "acquisition":
        if len(parts) < 2:
            return None
        sub = parts[1]
        if sub in ACQUISITION_FOUNDATION_DIRS:
            return FOUNDATION
        if sub in ACQUISITION_COORDINATION_DIRS:
            return COORDINATION
        return None
    return top if top in TOP_LEVEL_DIRS else None


def resolve_target(source_rel: PurePosixPath, include: str) -> PurePosixPath | None:
    """把 include 解析为相对 native/src 的路径；与分层无关的写法返回 None。"""
    target = PurePosixPath(include.replace("\\", "/"))
    if target.parts and target.parts[0] in TOP_LEVEL_DIRS:
        return target
    resolved = os.path.normpath(str(PurePosixPath(source_rel.parent) / target))
    parts = PurePosixPath(resolved).parts
    if not parts or parts[0] in {"..", "."} or parts[0] not in TOP_LEVEL_DIRS:
        return None
    return PurePosixPath(resolved)


def iter_includes(stripped: str):
    """返回 (行号, include 原文)。"""
    for match in INCLUDE_RE.finditer(stripped):
        yield stripped.count("\n", 0, match.start()) + 1, match.group(1)


def dsp_type_names(src_root: Path) -> frozenset[str]:
    """收集 dsp 头文件里声明的类型名，用于区分类型构造与函数调用。"""
    names: set[str] = set()
    dsp_dir = src_root / "dsp"
    if not dsp_dir.is_dir():
        return frozenset()
    for path in dsp_dir.iterdir():
        if path.suffix not in {".h", ".hpp"}:
            continue
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        names.update(match.group(1) for match in DSP_TYPE_DECL_RE.finditer(text))
        names.update(match.group(1) for match in DSP_TYPE_ALIAS_RE.finditer(text))
    return frozenset(names)


def _match_include_exception(
    exceptions: tuple[IncludeException, ...], rel: PurePosixPath, include: str
) -> int | None:
    for index, exception in enumerate(exceptions):
        if fnmatch(rel.as_posix(), exception.source) and fnmatch(include, exception.include):
            return index
    return None


def scan_includes(
    src_root: Path, exceptions: tuple[IncludeException, ...] = INCLUDE_EXCEPTIONS
) -> list[str]:
    findings: list[str] = []
    hits = [0] * len(exceptions)
    for path in iter_sources(src_root):
        rel = PurePosixPath(path.relative_to(src_root).as_posix())
        source_group = group_for(rel)
        if source_group is None:
            findings.append(
                f"{rel}: unregistered layer directory; "
                f"add it to scripts/common/check_layering.py and docs/architecture-design.md"
            )
            continue
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for lineno, include in iter_includes(text):
            target = resolve_target(rel, include)
            if target is None:
                continue
            target_group = group_for(target)
            if target_group is None:
                continue
            if target_group in ALLOWED_GROUPS[source_group]:
                continue
            matched = _match_include_exception(exceptions, rel, include)
            if matched is not None:
                hits[matched] += 1
                continue
            findings.append(
                f"{rel}:{lineno}: include \"{include}\" crosses layers: "
                f"{source_group} does not depend on {target_group}"
            )
    for exception, count in zip(exceptions, hits):
        if count == 0:
            findings.append(
                f"stale exception: {exception.source} -> {exception.include} "
                f"matched no violation; delete it or fix the pattern"
            )
    return findings


def scan_dsp_calls(
    src_root: Path, exceptions: tuple[DspCallException, ...] = DSP_CALL_EXCEPTIONS
) -> list[str]:
    findings: list[str] = []
    types = dsp_type_names(src_root)
    hits = [0] * len(exceptions)
    ui_dir = src_root / "ui"
    for path in iter_sources(ui_dir):
        rel = PurePosixPath(path.relative_to(src_root).as_posix())
        text = mask_literals(strip_comments(path.read_text(encoding="utf-8", errors="replace")))
        for match in DSP_CALL_RE.finditer(text):
            function = match.group(1)
            if function in ALLOWED_DSP_CALLS or function in types:
                continue
            matched = None
            for index, exception in enumerate(exceptions):
                if fnmatch(rel.as_posix(), exception.source) and exception.function == function:
                    matched = index
                    break
            if matched is not None:
                hits[matched] += 1
                continue
            lineno = text.count("\n", 0, match.start()) + 1
            findings.append(
                f"{rel}:{lineno}: ui calls dsp::{function}(); "
                f"move the computation to dsp/ or controller/ and expose the result"
            )
    for exception, count in zip(exceptions, hits):
        if count == 0:
            findings.append(
                f"stale exception: {exception.source} -> dsp::{exception.function} "
                f"matched no violation; delete it or fix the pattern"
            )
    return findings


def scan(
    src_root: Path = DEFAULT_SRC_ROOT,
    include_exceptions: tuple[IncludeException, ...] = INCLUDE_EXCEPTIONS,
    dsp_call_exceptions: tuple[DspCallException, ...] = DSP_CALL_EXCEPTIONS,
) -> list[str]:
    """返回全部违规描述；空列表表示通过。"""
    return scan_includes(src_root, include_exceptions) + scan_dsp_calls(src_root, dsp_call_exceptions)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check layering rules for native/src includes and ui dsp calls."
    )
    parser.add_argument("--src-root", type=Path, default=DEFAULT_SRC_ROOT, help="native/src directory")
    args = parser.parse_args(argv)
    findings = scan(args.src_root)
    if findings:
        print("layering check failed:", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        return 1
    print("layering check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
