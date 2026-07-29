#!/usr/bin/env python3
"""静态国际化检查。

校验 native/i18n/bf_app_*.ts 翻译源文件的完整性与一致性，并扫描 C++ 源码中
i18n.text(...) 字面量引用的 key 是否存在、是否出现用户可见的硬编码英文文案。

运行时已切换为 Qt6 .qm/QTranslator；.ts 仅作为 lrelease 源文件，故此脚本只做
静态校验，不涉及运行时加载路径。
"""
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
I18N_DIR = ROOT / "native" / "i18n"
NATIVE_SRC = ROOT / "native" / "src"
ERROR_CODES_PATH = ROOT / "contracts" / "error_codes.json"

# 全部 locale 的 .ts 源文件（stable + pseudo + 未审校）。
# stable 用户菜单仅暴露 zh_CN/en_US；pseudo 仅测试；其余 locale 保留源文件但不产出 .qm。
LOCALES = ["zh_CN", "zh_TW", "en_US", "ja_JP", "ko_KR", "de_DE", "fr_FR", "es_ES", "pseudo"]

PLACEHOLDER_RE = re.compile(r"\{[a-zA-Z_][a-zA-Z0-9_]*\}")
CHINESE_RE = re.compile(r"[\u4e00-\u9fff]")

# .ts message 上不应出现的 translation type（表示翻译未完成或已废弃）。
# Qt Linguist 用 type="unfinished"/"vanished"/"obsolete" 标记问题条目。
BAD_TRANSLATION_TYPES = {"unfinished", "vanished", "obsolete"}

# Spike UI 硬编码英文的已知豁免路径（精确到文件级，非目录级）。
# v1 Spike 为非发布线产品（独立可执行 bf_spike），其 UI 文案暂不接入 i18n。
# 按批准口径：不全面翻译 Spike UI，其硬编码英文作为已知范围以窄路径豁免，
# 不产生大范围误报。spike_widgets 的 paintEvent 标题/轴标签同理。
# 仅豁免这两个文件；spike_widgets.h 的成员默认值不匹配 user-visible API 模式，无需豁免。
SPIKE_EXEMPT_FILES = {
    "ui/spike_window.cpp",
    "ui/spike_widgets.cpp",
}


def load_error_codes() -> list[dict[str, object]]:
    registry = json.loads(ERROR_CODES_PATH.read_text(encoding="utf-8"))
    if registry.get("schema_version") != 1:
        raise AssertionError("contracts/error_codes.json schema_version must be 1")
    return list(registry["error_codes"])


def load_messages(locale: str) -> dict[str, str]:
    """加载 locale 的 .ts，返回 source->translation 字典。

    任何缺少 source、空翻译、重复 key、unfinished/vanished/obsolete 条目均在此报错。
    """
    path = I18N_DIR / f"bf_app_{locale}.ts"
    if not path.exists():
        raise AssertionError(f"missing translation file: {path}")
    root = ET.parse(path).getroot()
    messages: dict[str, str] = {}
    for message in root.findall(".//message"):
        source = message.findtext("source")
        if not source:
            raise AssertionError(f"{path}: message without source")
        # 检测重复 key。
        if source in messages:
            raise AssertionError(f"{path}: duplicate key {source!r}")
        translation_elem = message.find("translation")
        if translation_elem is None:
            raise AssertionError(f"{path}: missing <translation> for {source!r}")
        # 拒绝 unfinished/vanished/obsolete。
        ttype = translation_elem.get("type", "")
        if ttype in BAD_TRANSLATION_TYPES:
            raise AssertionError(
                f"{path}: {source!r} has translation type {ttype!r} "
                f"(must be finished, not unfinished/vanished/obsolete)"
            )
        translation = translation_elem.text
        if translation is None or translation == "":
            raise AssertionError(f"{path}: empty translation for {source!r}")
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
        # reader.open_failed: error_codes.json declares params ["path", "reason"] —
        # path is wire-only for logging and intentionally omitted from user-facing
        # translations. TS only carries {reason}. See the _note in error_codes.json.
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


# ---------------------------------------------------------------------------
# C++ 源码中 i18n.text(...) 字面量 key 引用校验。
#
# 匹配以下模式（key 为字面量字符串，非动态拼接）：
#   i18n.text("...")
#   i18n_->text("...")
#   i18n->text("...")
#   i18n.text(QStringLiteral("..."))
#   ...->text(QStringLiteral("..."))
#   ...->text(QString("..."))  （仅当字面量内容形如 app.*/error.* 时视为 key）
#
# 动态拼接（如 QString("error.%1").arg(...)）不在此校验，由 error_codes 契约覆盖。
# ---------------------------------------------------------------------------
TEXT_LITERAL_PATTERNS = [
    # QStringLiteral("...") 形式
    re.compile(r'(?:\.|->)text\(\s*QStringLiteral\(\s*"((?:app|error)\.[^"]*)"\s*\)'),
    # 直接 "..." 形式（仅匹配 app.*/error.* 开头的 key）
    re.compile(r'(?:\.|->)text\(\s*"((?:app|error)\.[^"]*)"'),
]

# statusKey_ / emptyStateKey_ 等成员赋值的字面量 key。
# 这些字符串最终会传给 i18n_->text()，故也需校验。
KEY_ASSIGN_PATTERNS = [
    re.compile(r'statusKey_\s*=\s*"((?:app|error)\.[^"]*)"'),
    re.compile(r'emptyStateKey_\s*=\s*"((?:app|error)\.[^"]*)"'),
    # 成员默认值（头文件）：QString statusKey_ = "app.status.ready";
    re.compile(r'=\s*"((?:app|error)\.[^"]*)"\s*;'),
]

# QString("app.language.%1").arg(...) 这类动态 key 的前缀校验：
# app.language. 前缀在 .ts 中以 app.language.zh_CN 等形式存在，由 error_codes 之外
# 的 locale 列表覆盖。此处只收集字面量前缀做存在性提示。


def collect_literal_keys() -> set[str]:
    """扫描所有 C++ 源码，收集 i18n.text(...) 字面量引用的 key。"""
    keys: set[str] = set()
    for path in NATIVE_SRC.rglob("*"):
        if path.suffix not in {".cpp", ".h"}:
            continue
        text = path.read_text(encoding="utf-8")
        for pat in TEXT_LITERAL_PATTERNS:
            for m in pat.finditer(text):
                keys.add(m.group(1))
        # 仅对 statusKey_/emptyStateKey_ 赋值做 key 收集（避免误匹配普通字符串）。
        for pat in KEY_ASSIGN_PATTERNS[:2]:
            for m in pat.finditer(text):
                keys.add(m.group(1))
        # 头文件中的成员默认值（statusKey_ = "app.status.ready" 等）。
        if path.suffix == ".h":
            for m in KEY_ASSIGN_PATTERNS[2].finditer(text):
                keys.add(m.group(1))
    return keys


def check_literal_key_references() -> None:
    """校验代码中字面量引用的 i18n key 是否全部存在于 .ts 翻译文件。"""
    baseline = load_messages("zh_CN")
    known_keys = set(baseline)
    literal_keys = collect_literal_keys()
    missing = sorted(k for k in literal_keys if k not in known_keys)
    if missing:
        raise AssertionError(
            f"code references i18n keys not present in zh_CN .ts: {missing}"
        )


# ---------------------------------------------------------------------------
# 用户可见硬编码英文检测。
#
# 基于明确的用户可见 Qt API 调用模式，而非全文本扫描：
#   - setText("...")            QLabel/QPushButton/QAction/QCheckBox 等设可见文本
#   - setWindowTitle("...")     窗口标题
#   - setTitle("...")           菜单/Dock/分组标题
#   - addMenu("...")            顶级菜单
#   - addAction("...")          菜单/工具栏动作
#   - addTab(widget, "...")     标签页标题
#   - setTabText(...)
#   - setToolTip("...")         提示
#   - setHorizontalHeaderLabels(...)
#   - new QLabel("...", ...)    构造时传入可见文本
#   - new QAction("...", ...)   构造时传入可见文本
#   - new QPushButton("...")    构造时传入可见文本
#   - drawText(..., "...")      QPainter 直接绘制可见文本
#   - showMessage("...")        状态栏消息
#   - setStatusText("...")      自定义状态文本
#
# 豁免：
#   - 日志/CLI：std::cerr/std::cout/qWarning/qDebug/qFatal
#   - 协议/文件 filter（含通配符 *.* 的过滤器串）
#   - 单位符号（uV/Hz/s/dB/ms 等）
#   - 色值/样式表（#RRGGBB、QPalette 键）
#   - 技术词白名单（CSV/PSD/FFT/STFT/ISI/PSTH/Cluster 等纯技术标识）
#   - Spike UI 已知硬编码范围（spike_window.cpp/spike_widgets.*）
# ---------------------------------------------------------------------------

# 用户可见 API 调用模式：捕获第一个字符串字面量参数。
# 仅匹配 QStringLiteral("...") 和 "..." 两种字面量形式，不匹配变量/tr。
USER_VISIBLE_API_PATTERNS = [
    # setText / setWindowTitle / setTitle / setToolTip / setTabText / showMessage / setStatusText
    re.compile(r'\b(?:setText|setWindowTitle|setTitle|setToolTip|setTabText|showMessage|setStatusText)\(\s*QStringLiteral\(\s*"([^"]*)"\s*\)'),
    re.compile(r'\b(?:setText|setWindowTitle|setTitle|setToolTip|setTabText|showMessage|setStatusText)\(\s*"([^"]*)"'),
    # addMenu / addAction（单参数或首参数为字面量）
    re.compile(r'\baddMenu\(\s*QStringLiteral\(\s*"([^"]*)"\s*\)'),
    re.compile(r'\baddMenu\(\s*"([^"]*)"'),
    re.compile(r'\baddAction\(\s*QStringLiteral\(\s*"([^"]*)"\s*[\),]'),
    re.compile(r'\baddAction\(\s*"([^"]*?)"\s*[\),]'),
    # addTab(widget, "字面量")
    re.compile(r'\baddTab\(\s*[^,]+,\s*QStringLiteral\(\s*"([^"]*)"\s*\)'),
    re.compile(r'\baddTab\(\s*[^,]+,\s*"([^"]*)"'),
    # new QLabel("...", ...) / new QAction("...", ...) / new QPushButton("...", ...)
    re.compile(r'\bnew\s+(?:QLabel|QAction|QPushButton|QCheckBox|QGroupBox)\(\s*QStringLiteral\(\s*"([^"]*)"\s*[\),]'),
    re.compile(r'\bnew\s+(?:QLabel|QAction|QPushButton|QCheckBox|QGroupBox)\(\s*"([^"]*?)"\s*[\),]'),
    # drawText(..., "...")  —— QPainter 绘制可见文本（最后一个字符串参数）
    re.compile(r'\bdrawText\([^)]*?,\s*QStringLiteral\(\s*"([^"]*)"\s*\)'),
    re.compile(r'\bdrawText\([^)]*?,\s*"([^"]*)"\s*\)'),
    # setHorizontalHeaderLabels({"...", ...})
    re.compile(r'\bsetHorizontalHeaderLabels\('),
]

# 需要跳过的行上下文：日志/CLI/协议诊断，不是用户可见 UI 文案。
SKIP_LINE_CONTEXT = (
    "std::cerr", "std::cout", "qWarning", "qDebug", "qFatal", "qCritical",
    "qInfo", "fprintf", "printf",
)

# 纯技术标识白名单：这些是通用技术术语/缩写，不是需要本地化的用户文案。
TECHNICAL_WHITELIST = {
    "CSV", "PSD", "FFT", "STFT", "ISI", "PSTH", "CAR", "EEG",
    "Cluster", "Raster",  # Spike 标签页技术名（已知豁免范围）
}

# 单位/量纲符号：不本地化。
UNIT_PATTERN = re.compile(r'^[a-zA-Zμ]+/?[a-zA-Zμ]*$')


def _is_exempt_english(s: str, line: str, rel_path: str) -> bool:
    """判断一个捕获到的英文字符串是否应豁免硬编码检查。"""
    # Spike UI 已知硬编码范围。
    if rel_path in SPIKE_EXEMPT_FILES:
        return True
    # 空/纯空白。
    stripped = s.strip()
    if not stripped:
        return True
    # 技术词白名单（精确匹配）。
    if stripped in TECHNICAL_WHITELIST:
        return True
    # 带 mnemonics 的 &File 等（技术上需本地化，但 Spike 已豁免；非 Spike 路径
    # 若出现 &X 单字母也豁免——属于 Qt 快捷键助记符，非完整文案）。
    if re.fullmatch(r'&[A-Za-z]', stripped) or re.fullmatch(r'&[A-Za-z]\.{3}', stripped):
        return True
    # 文件过滤器（含通配符或 ;; 分隔的多 filter）。
    if '*' in s or ';;' in s:
        return True
    # 纯数字/数字格式串（如 "0.0", "100"）。
    if re.fullmatch(r'[\d.]+', stripped):
        return True
    # QTime 格式串（HH:mm:ss 等）。
    if re.fullmatch(r'[Hhms:z.]+', stripped):
        return True
    # 纯单位符号（uV/Hz/s/dB/ms 等）。
    if UNIT_PATTERN.fullmatch(stripped) and len(stripped) <= 6:
        return True
    # 占位符格式串（如 "%1=%2"）——纯模板，非用户文案。
    if re.fullmatch(r'[%\d={}a-zA-Z_ ]+', stripped) and '%' in stripped:
        return True
    return False


def _has_skip_context(line: str) -> bool:
    """该行是否处于日志/CLI/协议诊断上下文，应整体跳过。"""
    return any(ctx in line for ctx in SKIP_LINE_CONTEXT)


def check_no_hardcoded_chinese_in_ui() -> None:
    """检测 C++ 源码中的硬编码中文（注释与日志格式串除外）。

    简体中文注释允许（项目约定）。日志/CLI 诊断格式串允许中文（不本地化）。
    仅检测出现在用户可见字符串字面量中的中文。
    """
    for path in NATIVE_SRC.rglob("*"):
        if path.suffix not in {".cpp", ".h"}:
            continue
        text = path.read_text(encoding="utf-8")
        for lineno, line in enumerate(text.splitlines(), start=1):
            # 日志/CLI 诊断格式串允许中文。
            if _has_skip_context(line):
                continue
            # 注释行跳过。
            stripped_line = line.lstrip()
            if stripped_line.startswith("//") or stripped_line.startswith("*"):
                continue
            # 仅检测字符串字面量内的中文。
            for m in re.finditer(r'"([^"]*)"', line):
                if CHINESE_RE.search(m.group(1)):
                    raise AssertionError(
                        f"hardcoded Chinese text in string literal at {path}:{lineno}"
                    )


def check_no_hardcoded_english_in_ui() -> None:
    """检测用户可见 API 调用中的硬编码英文文案。

    仅扫描明确的用户可见 Qt API 模式，不扫描日志/CLI/协议/文件 filter/单位。
    Spike UI 硬编码作为已知范围以窄路径豁免。
    """
    violations: list[str] = []
    for path in NATIVE_SRC.rglob("*"):
        if path.suffix not in {".cpp", ".h"}:
            continue
        rel_path = str(path.relative_to(NATIVE_SRC))
        # 测试文件不扫描（非目标）。
        if "_test" in path.name or "smoke" in path.name:
            continue
        text = path.read_text(encoding="utf-8")
        for lineno, line in enumerate(text.splitlines(), start=1):
            if _has_skip_context(line):
                continue
            # 注释行跳过（简单启发：去除前导空白后以 // 或 * 开头）。
            stripped_line = line.lstrip()
            if stripped_line.startswith("//") or stripped_line.startswith("*"):
                continue
            for pat in USER_VISIBLE_API_PATTERNS:
                for m in pat.finditer(line):
                    captured = m.group(1) if m.lastindex else ""
                    if not captured:
                        continue
                    if _is_exempt_english(captured, line, rel_path):
                        continue
                    # 仅检测含拉丁字母且像英文文案的串。
                    if not re.search(r'[A-Za-z]', captured):
                        continue
                    # 单个短词（<=4 字母）若无空格，视为可能的技术标识，跳过。
                    words = captured.split()
                    if len(words) == 1 and len(captured) <= 4:
                        continue
                    violations.append(
                        f"hardcoded English UI text in {rel_path}:{lineno}: {captured!r}"
                    )
    if violations:
        raise AssertionError(
            "hardcoded English UI text detected (use i18n.text() instead):\n  "
            + "\n  ".join(violations)
        )


def main() -> int:
    try:
        check_keys_and_placeholders()
        check_literal_key_references()
        check_no_hardcoded_chinese_in_ui()
        check_no_hardcoded_english_in_ui()
    except AssertionError as exc:
        print(f"i18n check failed: {exc}", file=sys.stderr)
        return 1
    print("i18n check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
