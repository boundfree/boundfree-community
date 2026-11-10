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
- 旧 Python/PySide6 参考仓库保存在跨仓库工作空间的 `archive/boundfree-eeg-upper-computer`（见工作空间 AGENTS.md），仅用于只读参考和黄金文件；不能假定它与当前检出目录相邻。
- 除非任务明确要求，不要修改旧参考仓库。
- 不要把旧参考仓库的未提交改动回滚、覆盖或迁入本仓库。

## Required Reading

从 [文档索引](docs/README.md) 按任务选择阅读，不再要求逐份加载历史里程碑报告：

| 任务 | 当前文档 |
| --- | --- |
| 产品范围、优先事项与验收 | `docs/roadmap.md`、`docs/feature-parity-checklist.md` |
| 使用浏览、导出与专用分离 | `docs/analysis-guide.md` |
| 版本、IPC、错误码、i18n 契约 | `docs/contracts.md` |
| 新功能、分支、PR 评审返修与合入 | `docs/development-guide.md`、`docs/contribution-sop.md` |
| 安装包、版本流转与 Release | `docs/release-sop.md` |
| 模块边界 / 本地环境 | `docs/architecture-design.md`、`docs/toolchain-wsl2.md` |
| 开源许可 / 付费扩展归属 | `LICENSE`、`NOTICE`、`legal/LICENSING.md` |

历史计划、报告和旧设计不保留在仓库，完成或被替代后直接删除，靠 Git 历史追溯；删除前把仍有效的说明、约束和未验收项并入当前文档，删除不表示待验收项已通过。历史正文中的“当前”“下一步”、旧逐提交版本和子代理指令不适用于新任务。

## Repository Delivery Skill

- 本仓库修改与交付使用仓库专用技能 [community-delivery](.agents/skills/community-delivery/SKILL.md)。代理开始修改前应加载该技能；不支持技能发现的工具直接读取该文件。
- 人工开发者按 [提交与交付 SOP](docs/contribution-sop.md) 操作；版本调整与发版继续以 [版本与发布 SOP](docs/release-sop.md) 为准。支持仓库技能的工具可显式调用 `$community-delivery`。
- 技能不扩大任务授权，不自动要求对外发布。PR 使用 `.github/pull_request_template.md`，记录版本判断、验证证据与未验收项。
- 流程规则变化时，在同一 PR 核对规范、SOP、技能和模板，只同步受影响的内容；不要复制另一套互相矛盾的流程。

## Development Stage

- 本机日常开发源码位于 Linux 主目录 `/home/vsonic12138/workspace/BoundFree_Workspace/boundfree-community`，日常在 `master` 上按开发规范建短分支；`.worktrees/community-ica` 仅为兼容入口。`F:\workspace\BoudFree\development\boundfree-community` 是从 Linux 单向同步的 Windows 编译副本，不在副本编辑、暂存或提交。副本清单 `.boundfree-windows-mirror.json` 与安全同步操作见 `docs/toolchain-wsl2.md`；不要用旧分支或旧保留工作树覆盖副本。
- 当前版本为 `0.2.2-beta.1`。
- 主线已接入固定提交的 Fluent-Qt 核心：设备、工作台与 ICA 的按钮/下拉框、文本输入、复选框、滑块、菜单与折叠区，并以 Fluent token 统一原生数值框和标签页。Windows/WSL Qt6.8.2编译与交互回归通过，入口见 `docs/fluentqt-integration.md`；真实硬件与新安装包仍待验收。
- 激活窗口只保留在线申请与离线许可证两种方式：打开即查询授权状态，管理员续期后自动领取；在线授权的吊销检查在主窗口显示后后台进行，确认停用即要求重新激活；Linux 机器码读取 `/etc/machine-id`，读不到时禁止激活；真实授权服务器、Windows 桌面与高 DPI 仍待验收。
- 开发分支含通用设备采集候选链路、RT-BCI Wi-Fi 适配及菜单栏“设备”中的连接管理窗口（复用应用主题，默认简化、高级参数折叠），连接后自动接入主界面同一个波形组件，入口与限制见 `docs/device-acquisition.md`；BFSP 1.0 为候选线协议，真实设备与新传输方式按各自验收，不把虚拟设备回归当作硬件交付。
- ICA 开发候选统一使用菜单栏“设备”的连接窗口；连接后波形直接显示，首次进入“脑电/眼电/肌电分离”自动准备建模片段，面板不再提供设备连接入口，文件与教学记录统一放在文件菜单，“打开数据文件”名称固定，行为随浏览/分离工作区切换，设备录制明确为文件夹，合成教学数据明确为内置分离示例，页面分区显示标题、四步流程与提示、当前分析输入与可取样后台设备，波形/分离共用细线与三点拖动分隔条，通道用途提供上下文解释，右上角统一使用说明与输入旁采集信息打开非模态分页窗口，成分检查采用可选列表与分类单选项，支持前后切换及波形/频谱/参考标签，分组标题显示观测通道与贡献IC，左侧/中间/底部重复说明按钮已移除，分组及IC波形支持局部快捷缩放/平移，时间同步、IC幅值独立，坐标显示刻度/实际单位，频谱从零显示PSD，底部设备采集栏在两页共用，查看操作按当前页面处理，连续 ICA 可仅暂停显示并返回最新帧，录制提示移至现有底部状态栏；按准备信号/建立模型/检查成分/查看结果引导，仍可选择多流和重新获取连续原始片段；文件/设备复用既有分离与导出流程，设备缓存最多10秒，至少5秒连续数据，输入精度为float32投影；直接打开原文件保持float64读取。取样后固定输入，再次获取同一流保留用途，失败保留旧结果；真实设备联动效果仍待验收。
- 本地模拟器支持默认 ICA 混合信号与原周期波形预设。ICA预设含三路固定混合观测和眼电/肌电参考，float32/uV、250/500/1000Hz连续合成；分离页按版本化通道身份预填用途并支持教学分类，250/500Hz可连续ICA。录制保留预设/混合矩阵元数据，回放可再次分离；数学合成验证与生理效果验收分别记录。
- 同一开发候选增加设备连续 ICA：先用设备片段建立并人工检查模型，再把固定矩阵和均值应用于新原始样本，滚动显示分组与成分；当前限同源250/500Hz、输出率一致、陷波关闭。建模取样不足5秒自动等待并可取消；通道用途提供显式批量观测操作。重连/换配置/停采集停止旧模型，缺口断开，原始录制独立；连续结果暂不提供整段保存。入口与限制见 `docs/analysis-guide.md` 的设备连续ICA章节，验证状态以本轮记录为准。
- 已有 EEG/LFP 浏览、处理、播放、标注、视频、录制/导出与专用脑电/眼电/肌电分离；当前重点是实际记录、用户走查和发布验收，具体范围与限制见路线及清单。
- Windows/Linux 云端构建和安装态 smoke 已通过；Beta1 tag、独立构建与 GitHub 草稿已生成并核对，公开发布仍待实机/真实授权及分发许可验收，Gitee 尚无公开 Release。干净机、桌面、高 DPI 和组织签名仍分别验收。历史 GPU 证据不能代替当前所有负载结论。
- `bf_spike` 为保留的历史原型，不是 Community 当前发布承诺；自研硬件/BLE 闭环需另行确定范围。
- 按交付阶段更新版本，日常提交由 Git 记录，CI 自动产生唯一构建标识。

## Collaboration Platforms

GitHub 是同一 Community 项目的研发主仓库，统一管理 Issue/PR/验收；Gitee 是国内源码和 Release 镜像。权限、远端配置和源码同步以 [提交 SOP 第 0 节](docs/contribution-sop.md#0-两个托管地址与协作顺序) 为准，发布交接见 [发布 SOP](docs/release-sop.md)。

## Branch And Version Rules

- 任务分支、依赖 PR、评审方式、merge commit 和清理规则见 [开发规范](docs/development-guide.md#branch-model)，建分支到合入的命令按 [提交 SOP](docs/contribution-sop.md)；所有 GitHub 主线修改经 PR，不向 GitHub `master` 直推开发提交。
- `version.json` 是产品版本真值，普通提交不涨版本；阶段变更按 [发布 SOP 第 3 节](docs/release-sop.md#3-调整交付阶段版本) 使用同步工具。提交前运行 `python3 scripts/common/version_sync.py check`。
- 产品版本与 `ipc_protocol_version` 独立维护。
- 普通 CI 只生成内部 dev 包；Release 使用 `community-v*` annotated tag 并先生成草稿。对外发布按发布 SOP 验收，不覆盖 tag 或同版本安装包。

## Technical Constraints

- 主程序使用 C++17；Qt 共同基线由 `config/qt-version.txt` 管理，当前6.8.2。默认精确校验，非基线实验须显式 `BF_ALLOW_OTHER_QT=ON` 并使用独立构建目录。WSL入口与环境见 `docs/toolchain-wsl2.md`。
- 构建系统使用 CMake；依赖与可执行目标以当前 `native/CMakeLists.txt` 为准，不把旧规划中的选型当作已实现。
- 文件格式读写全部经 Python 边车完成，主程序不直接解析科研文件格式。
- IPC 初版使用 JSON 控制消息和长度前缀二进制 EEG payload。
- 正式 IPC payload 字节序使用小端。
- 共享处理窗口形状为 `[n_channels, n_samples]`，逐通道携带单位、时间坐标和来源；电压统一到 `uV`，原始 count 和其他物理量保留实际单位。工具按采样率、连续性、单位兼容和样本量判断能力，禁止按来源整组禁用。浏览/处理投影仍为 float32，原始录制保留 typed 数据；专用原文件分离入口保持 float64。
- 滤波和 ICA 使用自有 C++ 数值实现，频谱已采用 pocketfft。禁止引入 FFTW、KFR 等 GPL 风险依赖。
- 授权校验保留在 C++ 原生侧，并兼容旧版密钥语义。
- 官方包保留现有整机激活，验收并发布后允许公开下载、使用仍需有效授权；免费核心免付费授权与扩展权益留待后续实现验收，不因许可调整宣称已完成。
- i18n 使用 Qt Linguist `.ts/.qm`，边车和后端只返回错误码与结构化参数。

## Implementation Rules

- 当前实施遵循任务范围与当前文档索引，不擅自恢复历史计划中的旧任务；未验收项以 `docs/feature-parity-checklist.md` 为准。
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
- 本机 WSL2 内存约 12GiB，其中 Claude/Codex/Orca 等常驻约 2GiB；`main.cpp`、`main_window.cpp` 等 Qt 大文件单个编译进程可达 1–1.5GiB。构建与测试并行度不超过 4（`cmake --build <dir> --parallel 4`），禁止不带数字的 `-j`/`make -j`，它会同时启动全部编译任务并耗尽内存、导致 WSL 重启。`tests/native/test_m8_license_gate.py` 会在临时目录联网拉取 Fluent-Qt 并完整编译 `bf_app`，最耗内存。
- 本机完整 pytest 按目录或测试文件分批串行运行，日志写到 `/mnt/boudfree-dev` 下而不是 `/tmp`（WSL 重启会清空 `/tmp`）；长时间运行时监控可用内存，低于约 3GiB 即停止。Python 原生测试默认查找 `native/build`，使用外置构建目录时注意测试的查找规则（见 `docs/toolchain-wsl2.md`）。

## Safety

- 不要执行破坏性 Git 操作，例如 `git reset --hard`、强制覆盖分支或删除用户改动，除非用户明确要求。
- 不要提交构建产物、安装包、私钥、授权 keygen 私有材料或临时环境目录。
- 不要覆盖已发布 tag。
- 不要引入与闭源商业分发冲突的许可证依赖。
- 不得对 Apache-2.0 核心附加禁止商用、修改或再分发的限制；第三方许可与归属不得被本项目许可覆盖。`legal/LICENSE.txt`、`legal/NOTICE.txt` 必须与根目录 `LICENSE`、`NOTICE` 一致。
