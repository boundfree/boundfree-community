# 删除未发布语言的翻译源文件

日期：2026-11-05　分支：`chore/remove-unshipped-locales`（基于 origin/master `5e8602f`）

## 背景

产品只发布 zh_CN、zh_TW、ja_JP、en_US（另有仅测试用的 pseudo）。ko_KR、de_DE、fr_FR、es_ES 的 `.ts` 从未产出 `.qm`、不进菜单和安装包，却要在每次改文案时同步维护。用户决定直接删除。

## 范围

1. 删除 `native/i18n/bf_app_{ko_KR,de_DE,fr_FR,es_ES}.ts`。
2. `scripts/common/check_i18n.py`：`LOCALES` 只保留 zh_CN、zh_TW、en_US、ja_JP、pseudo；改写上方注释，不再提“保留供未来扩展”。
3. `native/src/i18n/i18n_service.h`：删掉“de_DE/fr_FR/es_ES/ko_KR 的 .ts 源文件保留翻译”一句；“未来激活新 locale”的步骤改为包含“新增 .ts”。
4. `docs/contracts.md`：删除“以下 locale 保留 `.ts` 源文件……”一节及其列表，并修正下一段中“其余 locale 不产出 `.qm`”这类已无对象的说法。
5. 全仓库（`docs/archive/`、`archive/` 历史文档和 `version.md` 既有条目除外）搜索 `ko_KR|de_DE|fr_FR|es_ES|9 份|九种` 等，处理仍描述这四个文件存在的文字。
6. 如 `version.md` 有未发布版本的变更区，补一条简短记录；没有就不加。

## 不改

- `i18n_locale_format_test.cpp` 用 de_DE/fr_FR 作为“非正式语言应被拒绝”的输入，这是合法用例，保留；只在注释提到“这些 .ts 存在”时才调整措辞。
- `plot_style_test.cpp` 的 de_DE 是 QLocale 数字格式测试，与翻译文件无关，保留。
- `i18n_service.cpp` 中 `// e.g. "de_DE"` 的注释保留。
- 历史计划、归档文档、`archive/plans/20261104-*.md` 不改。

## 验证（WSL 内存约束：并行度不超过 4）

- `python3 scripts/common/check_i18n.py`
- 用 `/mnt/boudfree-dev/qt/6.8.2` 工具链配置并构建到 `/mnt/boudfree-dev/build/community-locales`（`--parallel 4`），运行 `ctest --test-dir <build> -R i18n --output-on-failure`。
- `git grep` 证明除上面列出的保留点外再无引用。
- `git diff --stat` 汇报。不要提交、推送或创建 PR。

## 审批与追加

- Codex 完成第 1–5 项；审批时追加删除其余 5 份 `.ts` 中已无用的 `app.language.{ko_KR,de_DE,fr_FR,es_ES}` 共 20 条（菜单只取 `supportedLocales()`）。
- 同日 GitHub 仓库改为公开，同一 PR 更新 README、提交 SOP、开发规范、发布 SOP 与交付技能中的可见性说明。
- 验证：`check_i18n.py`、完整构建（`--parallel 4`）、`bf_i18n_qm` 重新生成、`i18n_locale_format_test` failures=0。
