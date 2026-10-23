## 问题与变化

<!-- 说明具体问题、修改后的行为和范围；仅填写本次实际内容。 -->

## 版本判断

<!-- 普通提交保持产品版本；仅交付阶段变化时用 version_sync.py 同步五处。说明本次判断及版本。 -->

## 验证证据

<!-- 填写实际执行的命令/环境/结果；无法执行、跳过及仍待人工验收的事项明确说明。CI 结果对应最新提交。 -->

## 交付检查

- [ ] 已按 [提交与交付 SOP](https://github.com/Vsonic12138/boundfree-eeg-native/blob/master/docs/contribution-sop.md) 检查 diff、暂存文件及提交范围
- [ ] `version_sync.py check` 与 `git diff --check HEAD` 已通过，版本判断符合规范
- [ ] 已执行与改动相关的验证，记录未验收项；最新 PR CI 结果已核对
- [ ] 相关使用说明/契约已同步；若修改流程，已核对 SOP、skill、AGENTS 和模板

<!-- 合入后核对 master push 的 CI 与 build-info/SHA256；PR 成功不等同于主线包成功。普通提交不创建 tag 或 Release；发版另建 Community 发版验收 Issue 并按 release-sop.md 执行。 -->
