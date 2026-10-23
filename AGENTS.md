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

- 当前版本为 `0.2.2-dev`。
- M0 技术验证已完成，结论为 conditional go。
- 真实 GPU/显示门禁已通过（见 `docs/reports/m2-rendering-gpu-gate.md`）。
- M8 打包入口、Windows x64 安装包构建及命令行自动验收已于 2026-10-17 验证；VC++ 运行库作为安装前置依赖随包处理。干净 Windows 10/11 安装、覆盖安装、卸载、桌面人工验收、组织证书签名与正式授权兼容仍待验收（见 `docs/reports/m8-packaging-release-acceptance.md`）。
- 已实现专用脑电/眼电离线分离工作区、自有 FastICA、人工标签撤销与 float64 可追溯八文件导出；本地数值、文件协议与 Qt 工作流有回归，真实电极效果、新手用户研究和干净机安装仍需独立验收。入口与具体格式能力见 `docs/reports/offline-eeg-separation.md`。
- 自研硬件采集仍属于 v1.1/v2，不纳入 M8 发布门禁。
- 产品版本当前为 `0.2.2-dev`；按交付阶段更新版本，日常提交由 Git 记录，CI 自动产生唯一构建标识。

## Branch And Version Rules

- 日常开发基于 `master`。
- 功能开发从 `master` 切 `feature/*`、`fix/*`、`spike/*` 或 `docs/*`；当前阶段不绑定历史 `feature/m1-foundation` 分支。
- 使用 `master` 加短功能分支；历史 `release` 分支仅供追溯，不再要求维护第二条发布线。
- 已发布版本紧急修复从正式 tag 切 `hotfix/*`，修复同步回 `master` 后发布新版本。
- `version.json` 是产品版本和 IPC 协议版本的单一真值来源。
- 根目录 `version.md` 记录交付阶段变化，日常提交详情保留在 Git。
- 需要切换交付阶段时运行 `python3 scripts/common/version_sync.py next ...` 或 `set ...`，同步 `version.json`、`version.md`、`README.md`、`AGENTS.md`、`installer/BoundFreeEEG.iss`。普通提交无需递增产品版本；提交前运行 `python3 scripts/common/version_sync.py check`。
- 默认只使用 `dev`、`beta.N`、`stable`。CI 构建开发包时临时追加运行编号、重跑次数和提交 SHA，不回写仓库。
- `master` 每次推送自动构建 Windows 安装包和 Ubuntu 24.04 归档；Release 只由人工触发并生成草稿，使用 `community-v*` annotated tag，禁止覆盖已发布版本。
- 具体操作见 `docs/release-sop.md`；每次对外发版使用本仓库 Release checklist Issue 模板。
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
- 代码和文档应保持小步提交。仅交付阶段变化时同步产品版本与 `version.md`。提交信息统一使用中文 Conventional Commits 格式（`type: 中文摘要`），细则见 `docs/development-guide.md` 的 Commit Rules。允许的 type：`feat`、`fix`、`docs`、`test`、`chore`、`spike`、`refactor`、`perf`。type 用英文，subject 与 body 用中文。

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
