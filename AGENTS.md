# AGENTS.md

本文件为 `boundfree-eeg-native` 仓库的开发指导。所有自动化代码代理和协作开发者在修改本仓库前应先阅读本文件。

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

开始实现前，按任务类型阅读对应文档：

| 任务类型 | 必读文档 |
| --- | --- |
| 架构或模块边界 | `docs/architecture-design.md` |
| 分支、版本、发布 | `docs/git-development.md` |
| 文档新增或改写 | `docs/documentation-style.md` |
| 里程碑实施 | `docs/plans/README.md` 与对应 `docs/plans/milestone-*.md` |
| 功能迁移与验收 | `docs/feature-parity-checklist.md` |

## Development Stage

- 当前阶段为 `0.1.0-dev`。
- M0 技术验证已完成，结论为 conditional go。
- M1 是下一阶段重点：正式 CMake/Qt6 工程骨架、Python 边车生命周期、IPC、授权、i18n 和空主窗口。
- 进入 M2 波形浏览前，必须在真实 GPU/显示环境复测 M0 渲染 spike。

## Branch And Version Rules

- 日常开发基于 `master`。
- `release` 是稳定发布分支；当前可作为占位分支，但首个 `rc` 或 `stable` 版本前不得打正式 tag。
- 功能开发从 `master` 切 `feature/*`、`fix/*`、`spike/*` 或 `docs/*`。
- 已发布版本紧急修复从 `release` 或正式 tag 切 `hotfix/*`。
- `version.json` 是产品版本和 IPC 协议版本的单一人工维护来源。
- 不要在普通 feature 分支上修改产品版本号。
- 产品版本和 `ipc_protocol_version` 必须分开维护。

## Technical Constraints

- 主程序使用 C++17 和 Qt6。
- 构建系统使用 CMake；正式依赖管理按计划使用 vcpkg。
- 文件格式读写全部经 Python 边车完成，主程序不直接解析科研文件格式。
- IPC 初版使用 JSON 控制消息和长度前缀二进制 EEG payload。
- 正式 IPC payload 字节序使用小端。
- EEG 数据形状为 `[n_channels, n_samples]`，单位为 `μV`。
- DSP 使用 Eigen 与 pocketfft。禁止引入 FFTW 或 KFR 等 GPL 风险依赖。
- 授权校验保留在 C++ 原生侧，并兼容旧版密钥语义。
- i18n 使用 Qt Linguist `.ts/.qm`，边车只返回 `error_code` 和结构化参数。

## Implementation Rules

- 优先遵循现有架构文档和里程碑计划。
- controller/service 不依赖 UI；UI 不承担文件解析、DSP 算法或 IPC 细节。
- 边车不返回用户可见的本地化文案。
- 新增共享接口时，同步更新相关文档和测试计划。
- 修改版本、发布、文档规范时，同步检查 `README.md`、`AGENTS.md` 和 `docs/git-development.md`。
- 代码和文档应保持小步提交，提交信息使用明确前缀，例如 `feat:`、`fix:`、`docs:`、`spike:`、`chore:`。

## Verification

- 声称构建、测试、格式或功能完成前，必须运行对应验证命令并检查输出。
- 文档变更至少运行 `git diff --check HEAD`。
- JSON 变更至少运行 `python3 -m json.tool <file>`。
- C++/Qt6 工程变更应运行对应 CMake configure/build。
- Python 边车变更应运行对应 pytest 或协议冒烟测试。
- 性能结论必须记录环境、命令、样本和结果，不得把 WSL/Xvfb 软件渲染结果当成真实 GPU 验收。

## Safety

- 不要执行破坏性 Git 操作，例如 `git reset --hard`、强制覆盖分支或删除用户改动，除非用户明确要求。
- 不要提交构建产物、安装包、私钥、授权 keygen 私有材料或临时环境目录。
- 不要覆盖已发布 tag。
- 不要引入与闭源商业分发冲突的许可证依赖。
