## 任务与分支

<!-- 关联 Issue（完成用 Closes #编号，部分实现用 Refs #编号）；base/head 分支、前置 PR（无则写无）及本次可检查的验收项。 -->

## 问题与变化

<!-- 说明具体问题、修改后的行为和范围；仅填写本次实际内容。 -->

## 版本判断

<!-- 普通提交保持产品版本；仅交付阶段变化时用 version_sync.py 同步五处。说明本次判断及版本。 -->

## 验证证据

<!-- 填写实际执行的命令/环境/结果；无法执行、跳过及仍待人工验收的事项明确说明。CI 结果对应最新提交。 -->

## 交付检查

- [ ] 分支/base 与任务匹配，前置依赖已注明；合入前所有前置 PR 已合入
- [ ] 已按 [提交与交付 SOP](https://github.com/boundfree/boundfree-community/blob/master/docs/contribution-sop.md) 检查 diff、暂存文件及提交范围
- [ ] `version_sync.py check` 与 `git diff --check HEAD` 已通过，版本判断符合规范
- [ ] 已执行与改动相关的验证，记录未验收项；最新 PR CI 结果已核对
- [ ] 相关使用说明/契约已同步；若修改流程，已核对 SOP、skill、AGENTS 和模板
- [ ] 新增实现符合开源核心 / 独立商业扩展边界，第三方来源与许可已保留；涉及许可时已核对 `LICENSE` / `NOTICE` 和打包副本

## 评审与合入记录

<!-- 初建 PR 可写待评审。合入前记录最新 head SHA、非作者评审或单人维护自审的实际结论、阻塞讨论处理结果及双平台 CI。返修/更新主线后重新核对；未满足不勾选。 -->

- [ ] 最新 head 包含当前主线，Windows/Linux PR CI 均成功，验收与评审要求满足

<!-- 默认 Create a merge commit；合入后单独核对 master push 的 CI 与 build-info/SHA256，按已核验 SHA 同步 Gitee，确认无依赖后清理本次分支。PR 成功不等同于主线包成功。Issue/PR 统一在 GitHub；普通提交不创建 tag 或 Release。发版在 GitHub 建 Community 发版验收 Issue，发布后按 release-sop.md 同步 Gitee 说明和国内下载、核对原包 SHA256，并回填同一 Issue。 -->
