# AGENTS.md

本文件为 `boundfree-community` 仓库的开发指导。自动化代码代理和协作开发者在修改本仓库前必须先阅读本文件。

## Communication

- 与用户沟通使用简体中文。
- 回复应直接说明事实、改动、验证结果和剩余风险。
- 不要把推测当成结论；涉及构建、测试、性能或发布状态时必须先验证。

## Repository Scope

- 本仓库是 BoundFree EEG 新一代 C++/Qt6 原生上位机。
- `../boundfree-eeg-upper-computer` 是旧 Python/PySide6 参考仓库，仅用于参考实现、边车迁移、性能基线和黄金文件。
- 除非任务明确要求，不要修改旧参考仓库。
- 不要把旧参考仓库的未提交改动回滚、覆盖或迁入本仓库。

## Required Reading

日常开发优先阅读以下当前有效文档：

| 当前阶段实施 | `docs/plans/m8-packaging-release.md`；实现与当前环境证据见 `docs/reports/m8-packaging-release-acceptance.md` |
| 已完成阶段证据 | `docs/reports/m2-format-acceptance.md`、`docs/reports/m2-rendering-gpu-gate.md`、`docs/reports/m3-dsp-acceptance.md`、`docs/reports/m4-spectral-acceptance.md`、`docs/reports/m5-playback-annotations-acceptance.md`、`docs/reports/m6-video-sync-acceptance.md`、`docs/reports/m7-record-export-acceptance.md` |
| 版本、IPC、错误码、i18n 契约 | `docs/contracts.md` |
| 分支、版本、文档、验证规范 | `docs/development-guide.md` |
| 架构或模块边界 | `docs/architecture-design.md` |
| 路线图和阶段门禁 | `docs/roadmap.md` |
| 功能迁移与验收 | `docs/feature-parity-checklist.md` |

`docs/archive/` 仅用于历史追溯，不作为日常实施入口。

## Development Stage

- 当前版本为 `0.1.76-dev`。
- M0 技术验证已完成，结论为 conditional go。
- 真实 GPU/显示门禁已通过（见 `docs/reports/m2-rendering-gpu-gate.md`）。
- M8 打包实现与 WSL2 Linux 可验证部分已完成；Windows x64 安装包、组织证书签名及干净 Windows 10/11 发布门禁尚未执行（见 `docs/reports/m8-packaging-release-acceptance.md`）。
- 自研硬件采集仍属于 v1.1/v2，不纳入 M8 发布门禁。
- 产品版本当前为 `0.1.76-dev`；每个有效提交必须递增产品版本并更新根目录 `version.md`。

## Branch And Version Rules

- 日常开发基于 `master`。
- 功能开发从 `master` 切 `feature/*`、`fix/*`、`spike/*` 或 `docs/*`；当前阶段不绑定历史 `feature/m1-foundation` 分支。
- `release` 是稳定发布分支；当前可作为占位分支，但首个 `rc` 或 `stable` 版本前不得打正式 tag。
- 已发布版本紧急修复从 `release` 或正式 tag 切 `hotfix/*`。
- `version.json` 是产品版本和 IPC 协议版本的单一真值来源。
- 根目录 `version.md` 从版本同步规范启用提交开始记录每个有效提交对应的版本、日期、作者、提交标题和提交正文。
- 每个合入 `master` 的有效提交必须运行 `python3 scripts/common/version_sync.py next ...`（或 `set ...`）更新版本。该命令自动同步五处版本号：`version.json`、`version.md`、`README.md`、`AGENTS.md`、`installer/BoundFreeEEG.iss`；禁止手工只改 `version.json` 绕过流程。提交前运行 `python3 scripts/common/version_sync.py check` 校验五处版本号一致（不一致会报 FAIL）。
- 产品版本和 `ipc_protocol_version` 必须分开维护。

## Technical Constraints

- 主程序使用 C++17 和 Qt6。
- 构建系统使用 CMake；正式依赖管理按计划使用 vcpkg。
- 文件格式读写全部经 Python 边车完成，主程序不直接解析科研文件格式。
- IPC 初版使用 JSON 控制消息和长度前缀二进制 EEG payload。
- 正式 IPC payload 字节序使用小端。
- EEG 数据形状为 `[n_channels, n_samples]`，单位为 `uV`。
- M3 当前滤波实现不依赖第三方 DSP 库；M4 接入 FFT 前必须引入许可安全的后端（计划使用 `pocketfft`）。禁止引入 FFTW、KFR 等 GPL 风险依赖。
- 授权校验保留在 C++ 原生侧，并兼容旧版密钥语义。
- i18n 使用 Qt Linguist `.ts/.qm`，边车和后端只返回错误码与结构化参数。

## Implementation Rules

- 当前实施优先遵循 `docs/plans/m8-packaging-release.md`、`docs/contracts.md`、`docs/roadmap.md` 和 `docs/feature-parity-checklist.md`。
- 后端不得返回用户可见的本地化文案；统一返回 `code + params`。
- UI 可见文本从第一版开始走 i18n，不新增硬编码中文。
- 新增共享接口时，同步更新相关契约文档和测试计划。
- 修改版本、发布、文档规范时，同步检查 `README.md`、`AGENTS.md` 和 `docs/development-guide.md`。
- 代码和文档应保持小步提交。每次有效提交应同步更新 `version.json` 和根目录 `version.md`。提交信息统一使用中文 Conventional Commits 格式（`type: 中文摘要`），细则见 `docs/development-guide.md` 的 Commit Rules。允许的 type：`feat`、`fix`、`docs`、`test`、`chore`、`spike`、`refactor`、`perf`。type 用英文，subject 与 body 用中文。

## Verification

- 声称构建、测试、格式或功能完成前，必须运行对应验证命令并检查输出。
- 文档变更至少运行 `git diff --check HEAD`。
- JSON 变更至少运行 `python3 -m json.tool <file>`。
- 版本变更必须运行 `python3 scripts/common/version_sync.py check`。
- C++/Qt6 工程变更应运行对应 CMake configure/build。
- Python 边车变更应运行对应 pytest 或协议冒烟测试。
- 性能结论必须记录环境、命令、样本和结果，不得把 WSL/Xvfb 软件渲染结果当成真实 GPU 验收。

## Safety

- 不要执行破坏性 Git 操作，例如 `git reset --hard`、强制覆盖分支或删除用户改动，除非用户明确要求。
- 不要提交构建产物、安装包、私钥、授权 keygen 私有材料或临时环境目录。
- 不要覆盖已发布 tag。
- 不要引入与闭源商业分发冲突的许可证依赖。
