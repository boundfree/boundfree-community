---
title: Community 云端构建与发布流程验收
doc_type: report
status: active
owner: BoundFree
last_updated: 2026-10-23
---

# Community 云端构建与发布流程验收

从逐提交递增版本改为交付阶段版本，保留 `0.2.2-dev`，新增临时开发构建标识、双平台流水线、独立 Release 草稿流程及本仓库发版验收 Issue 模板。SOP 见 [版本与发布](../release-sop.md)。

## 验证入口

- [实施 PR 与最新检查](https://github.com/Vsonic12138/boundfree-eeg-native/pull/1/checks)。
- [流程验收 Issue 与最新运行证据](https://github.com/Vsonic12138/boundfree-eeg-native/issues/2)。该 Issue 持续保存最终包、SHA256 与手动入口验证状态，避免每次 CI 更新版本文档。
- [首轮完成 Linux 分发验证的运行](https://github.com/Vsonic12138/boundfree-eeg-native/actions/runs/39948655306)：代码 `7d6975e`；Windows 在运行库定位步骤失败，Linux 完成全部步骤，不能把该运行整体当成双平台成功。

## 已取得证据

- 本地：版本流转、发布门禁、Windows 入口与发布资源回归 78 passed / 1 skipped；版本同步、i18n、10 项发布资产校验、原生应用增量构建及 actionlint 通过。
- Windows 云端：294 项 Python/契约/格式测试通过、15 项按平台或可选能力跳过；8 项离线分离/导出/连续播放回归全部通过；开发配置与启用公钥的分发配置均编译成功；Python 边车已冻结。
- Linux 云端：完整回归通过，启用授权的分发构建、冻结边车、Qt 与动态库组装、归档解压后版本/握手/工作区 smoke 通过，产物上传成功。精确测试计数及后续补充见验收 Issue。
- 版本与发布门禁使用真实临时 Git 历史和 annotated tag 验证：五处元数据同步、beta/稳定版流转、轻量 tag 拒绝、版本不匹配拒绝、未合入提交拒绝、跨仓库 Issue 拒绝、已有 Release 与 API 异常拒绝、稳定版人工验收确认、仅默认主线运行发布工作流。

## 实跑发现与修正

- Qt 安装 Action 曾替换指定的 Python 3.12 为 3.14，导致 SciPy 源码构建失败。已禁用其 Python 设置、固定 Actions 的提交、断言 Python 版本并使用 wheel。
- 启用授权的源码回归会在部分普通界面测试打开激活弹窗。开发回归与正式分发配置已分开，分发包继续启用仓库公钥；实际授权门禁另测。
- WSL 聚合脚本隐藏子测试日志。云端改为直接执行测试，并设置单测试超时。
- 新 PowerShell 会话中，`vswhere | Select-Object` 的子表达式未可靠设置 `LASTEXITCODE`。已改为直接读取进程的 ExitCode 和标准输出；后续 Windows Setup 安装/运行/卸载实跑结果维护在验收 Issue。

## 验收边界

PR 构建与默认分支合入触发、手动 CI、Release 手动入口是不同验证项，均在验收 Issue 分别记录。默认分支尚未包含工作流时，手动按钮不能视为已启用。首次 beta Release 草稿需要选定实际交付版本和 annotated tag；未经执行不得宣称已验收。

当前未配置组织签名证书，未完成干净 Windows 10/11、旧版升级、完整桌面、正式授权兼容或 Linux 真实桌面验收；本次不发布正式版。命令行 smoke 不代表真实电极分离效果或真实 GPU 验收。
