# AGENTS.md

本文件为 `boundfree-community` 仓库的开发指导。自动化代码代理和协作开发者在修改本仓库前必须先阅读本文件。

## Communication

- 与用户沟通使用简体中文。
- 回复应直接说明事实、改动、验证结果和剩余风险。
- 不要把推测当成结论；涉及构建、测试、性能或发布状态时必须先验证。

## Repository Scope

- 本仓库是 BoundFree EEG 新一代 C++/Qt6 原生上位机。
- 本仓库自有核心采用 Apache-2.0，含现有 ICA 与基本结果导出。第三方内容保留原许可；许可范围、贡献和商业扩展边界见 `legal/LICENSING.md`。
- 付费扩展源码与商业许可在独立私有仓库管理；本仓库只放开源核心和公开集成接口，不混入专有扩展代码。Pro 与授权服务端不因本仓库许可变化而开源。
- `../archive/boundfree-eeg-upper-computer` 是旧 Python/PySide6 参考仓库，仅用于只读参考和黄金文件。
- 除非任务明确要求，不要修改旧参考仓库。
- 不要把旧参考仓库的未提交改动回滚、覆盖或迁入本仓库。

## Required Reading

从 [文档索引](docs/README.md) 按任务选择阅读，不再要求逐份加载历史里程碑报告：

| 任务 | 当前文档 |
| --- | --- |
| 产品范围、优先事项与验收 | `docs/roadmap.md`、`docs/feature-parity-checklist.md` |
| 使用浏览、导出与专用分离 | `docs/analysis-guide.md` |
| 版本、IPC、错误码、i18n 契约 | `docs/contracts.md` |
| 分支、提交与交付 | `docs/development-guide.md`、`docs/contribution-sop.md` |
| 安装包、版本流转与 Release | `docs/release-sop.md` |
| 模块边界 / 本地环境 | `docs/architecture-design.md`、`docs/toolchain-wsl2.md` |
| 开源许可 / 付费扩展归属 | `LICENSE`、`NOTICE`、`legal/LICENSING.md` |

`docs/archive/` 统一保存旧计划、设计和报告。历史正文中的“当前”“下一步”、旧逐提交版本和子代理指令不适用于新任务；归档不表示待验收项已通过。

## Repository Delivery Skill

- 本仓库修改与交付使用仓库专用技能 [community-delivery](.agents/skills/community-delivery/SKILL.md)。代理开始修改前应加载该技能；不支持技能发现的工具直接读取该文件。
- 人工开发者按 [提交与交付 SOP](docs/contribution-sop.md) 操作；版本调整与发版继续以 [版本与发布 SOP](docs/release-sop.md) 为准。支持仓库技能的工具可显式调用 `$community-delivery`。
- 技能不扩大任务授权，不自动要求对外发布。PR 使用 `.github/pull_request_template.md`，记录版本判断、验证证据与未验收项。
- 流程规则变化时，在同一 PR 核对规范、SOP、技能和模板，只同步受影响的内容；不要复制另一套互相矛盾的流程。

## Development Stage

- 当前版本为 `0.2.2-beta.1`。
- 已有 EEG/LFP 浏览、处理、播放、标注、视频、录制/导出与专用脑电/眼电分离；当前重点是实际记录、用户走查和发布验收，具体范围与限制见路线及清单。
- Windows/Linux 云端构建和安装态 smoke 已通过；Beta1 tag、独立构建与 GitHub 草稿已生成并核对，公开发布仍待实机/真实授权及分发许可验收，Gitee 尚无公开 Release。干净机、桌面、高 DPI 和组织签名仍分别验收。历史 GPU 证据不能代替当前所有负载结论。
- `bf_spike` 为保留的历史原型，不是 Community 当前发布承诺；自研硬件/BLE 闭环需另行确定范围。
- 按交付阶段更新版本，日常提交由 Git 记录，CI 自动产生唯一构建标识。

## Collaboration Platforms

GitHub 是同一 Community 项目的研发主仓库，统一管理 Issue/PR/验收；Gitee 是国内源码和 Release 镜像。权限、远端配置和源码同步以 [提交 SOP 第 0 节](docs/contribution-sop.md#0-两个托管地址与协作顺序) 为准，发布交接见 [发布 SOP](docs/release-sop.md)。

## Branch And Version Rules

- 日常基于 `master` 使用短分支，具体分支规则见 [开发规范](docs/development-guide.md#branch-model)。
- `version.json` 是产品版本真值，普通提交不涨版本；阶段变更按 [发布 SOP 第 3 节](docs/release-sop.md#3-调整交付阶段版本) 使用同步工具。提交前运行 `python3 scripts/common/version_sync.py check`。
- 产品版本与 `ipc_protocol_version` 独立维护。
- 普通 CI 只生成内部 dev 包；Release 使用 `community-v*` annotated tag 并先生成草稿。对外发布按发布 SOP 验收，不覆盖 tag 或同版本安装包。

## Technical Constraints

- 主程序使用 C++17 和 Qt6。
- 构建系统使用 CMake；依赖与可执行目标以当前 `native/CMakeLists.txt` 为准，不把旧规划中的选型当作已实现。
- 文件格式读写全部经 Python 边车完成，主程序不直接解析科研文件格式。
- IPC 初版使用 JSON 控制消息和长度前缀二进制 EEG payload。
- 正式 IPC payload 字节序使用小端。
- EEG 数据形状为 `[n_channels, n_samples]`，单位为 `uV`。
- 滤波和 ICA 使用自有 C++ 数值实现，频谱已采用 pocketfft。禁止引入 FFTW、KFR 等 GPL 风险依赖。
- 授权校验保留在 C++ 原生侧，并兼容旧版密钥语义。
- 官方包保留现有整机激活，验收并发布后允许公开下载、使用仍需有效授权；免费核心免付费授权与扩展权益留待后续实现验收，不因许可调整宣称已完成。
- i18n 使用 Qt Linguist `.ts/.qm`，边车和后端只返回错误码与结构化参数。

## Implementation Rules

- 当前实施遵循任务范围与当前文档索引，不从归档计划自动恢复旧任务；未验收项以 `docs/feature-parity-checklist.md` 为准。
- 后端不得返回用户可见的本地化文案；统一返回 `code + params`。
- UI 可见文本从第一版开始走 i18n，不新增硬编码中文。
- 新增共享接口时，同步更新相关契约文档和测试计划。
- 修改版本、发布、文档规范时，同步检查 `README.md`、`AGENTS.md`、`docs/development-guide.md`、两份交付/发布 SOP、仓库技能和 PR/Issue 模板。
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
- 不得对 Apache-2.0 核心附加禁止商用、修改或再分发的限制；第三方许可与归属不得被本项目许可覆盖。`legal/LICENSE.txt`、`legal/NOTICE.txt` 必须与根目录 `LICENSE`、`NOTICE` 一致。
