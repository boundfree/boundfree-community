---
title: 架构与文档后续调整实施计划
doc_type: plan
status: active
owner: BoundFree
last_updated: 2026-11-10
---

# 架构与文档后续调整实施计划

2026-11-10 完成了第一轮整理：删除历史归档、重写架构文档与 `AGENTS.md`、精简提交 SOP、删除 Spike 原型、消除 `render → controller` 反向依赖、把自检入口移出 `main.cpp`、把设备文件归到 `acquisition/`。那一轮只做文件移动和文档，没有改任何逻辑。

本文列出审查中发现、但那一轮没有做的事项。每项写明现状证据、目标、步骤、验证方法和风险。条目完成后从本文删除；全部完成后删除本文。

## 怎么用这份计划

- **一项一个分支一个 PR**，流程按 [提交与交付 SOP](contribution-sop.md)。不要把多项合在一起做，除非条目里写明可以合并。
- **“执行方式”一栏**说明这项适合怎么做：
  - *机械*：只移动、改名或按清单替换，可以交给低成本模型按任务说明执行，再由人或强模型审查 diff。
  - *需设计*：要判断接口和职责边界，先写出具体拆分方案再动手，不适合直接交给低成本模型。
- **每项都要过同一道验证底线**：本地构建成功，`tests/native`（跳过 `test_m8_license_gate.py`）与基线相比没有新增失败，双平台 CI 通过。条目里只写额外的验证。
- 文中的行数和计数取自 2026-11-10 的代码，动手前重新量一次。

## 优先级总览

| 编号 | 事项 | 优先级 | 执行方式 | 规模 |
| --- | --- | --- | --- | --- |
| A4 | 拆分 `MainWindow` | 高 | 需设计 | 大，分多个 PR |
| A5 | 拆分两个超长构造函数 | 中 | 机械 | 中 |
| A6 | 自检入口按主题拆文件 | 中 | 机械 | 中 |
| A7 | `bf_core` 按层拆成多个构建目标 | 中 | 需设计 | 中 |
| A8 | 设备构建目标重新划分 | 低 | 需设计 | 小 |
| A9 | 原生测试统一注册并改按功能命名 | 低 | 机械 | 中 |
| A10 | 本机必然失败的三个测试改为按环境跳过 | 中 | 机械 | 小 |
| B2 | 用户说明与开发入口分开 | 中 | 需设计 | 中 |
| B3 | 发布 SOP 按提交 SOP 的方式精简 | 中 | 需设计 | 小 |
| B4 | 契约文档拆分 | 低 | 机械 | 小 |
| B5 | 工具链文档改名 | 低 | 机械 | 小 |
| C1 | 文档类 PR 不跑完整构建 | 中 | 需设计 | 小 |
| C2 | 仓库与工具的零散收尾 | 低 | 机械 | 小 |

建议顺序：A10 → A4（分步）→ 其余按需。

## A. 代码结构

### A4 拆分 `MainWindow`

**现状**：`ui/main_window.cpp` 3104 行、107 个成员函数、约 120 个成员指针、92 处 `connect`。它同时负责：搭建整个界面（`buildFixedWorkspace` 370 行、`buildAnalysisDock` 199 行、`buildBrowserControls` 163 行）、文件打开与显示、分析任务调度与结果渲染、录制、片段导出、设备与文件来源切换、浏览与分离工作区切换、全部界面文字刷新（`retranslateUi` 161 行）。

**目标**：`MainWindow` 只做三件事：装配各面板、在面板之间转发信号、管理当前来源与工作区切换。目标规模 1000 行以内。

**步骤**（每步一个 PR，前一步合入后再做下一步）：

1. **分析面板**：把 `buildAnalysisDock`、`requestCurrentAnalysis`、`renderAnalysisResult`、`refreshAnalysisPage` 及相关成员提取为 `ui/analysis_panel.{h,cpp}`。面板对外只暴露“设置输入窗口”“请求刷新”和“语言变化”三类接口。先做这一步，因为它与其他部分耦合最少，且数值计算已经移走。
2. **浏览控制面板**：提取 `buildBrowserControls`、`rebuildChannelControls` 和通道显隐、窗口、幅值相关的槽函数为 `ui/browser_controls_panel.{h,cpp}`。
3. **录制与导出**：提取 `buildRecordControls`、`exportSegment` 及其状态为 `ui/record_export_panel.{h,cpp}`，或并入已有的 `eeg_playback_panel`。
4. **来源协调**：把“当前浏览来源是文件还是设备”的状态与切换逻辑（`showFileOpenResult`、`refreshSignalTools` 及设备接入相关槽）提取为 `controller/browse_source_coordinator.{h,cpp}`，不含任何控件。
5. **收尾**：`buildFixedWorkspace` 只剩装配；`retranslateUi` 拆到各面板自己的 `retranslateUi`。

每一步的做法相同：先列出要搬走的成员变量和函数清单，确认它们对 `MainWindow` 其余部分的引用点，再定面板接口，最后搬迁。不在搬迁的同时改行为或改界面。

**验证**：每步都跑全部 `--smoke-ui-*` 入口和 `eeg_separation_workspace_test`；四种语言切换、明暗主题、窄窗口（波形/分析标签切换）各手动或用现有离屏截图检查一次。

**风险**：高。这是用户每天看到的主界面，信号槽连接多，时序依赖隐蔽（例如架构文档里记录的“嵌入式设备面板不得关闭文件交互”）。必须小步走，每步都能单独回退。发布验收（Beta1 实机验收）进行期间不要做这一项。

### A5 拆分两个超长构造函数

**现状**：`EegSeparationWorkspace` 的构造函数 512 行，`DeviceWorkspace` 的构造函数 424 行，界面搭建、信号连接、初始状态全写在一起。

**目标**：构造函数只调用若干个 `build*()` 和一个 `connectSignals()`。

**步骤**：按界面区块把构造函数体切成私有成员函数（例如分离工作区的“输入区”“通道用途区”“成分检查区”“分组预览区”），局部变量提升为成员或作为返回值传递。不改控件创建顺序，不改连接顺序。

**验证**：`eeg_separation_workspace_test`、`acquisition_ui_test`、`--smoke-ui-device-waveform`、`--smoke-ui-live-ica*`。

**风险**：中低。要小心构造顺序：有的控件在创建后立刻被后面的代码引用，切分时保持原顺序即可。

### A6 自检入口按主题拆文件

**现状**：`app/smoke/smoke_entries.cpp` 是一个 2529 行的函数，里面是 54 个 `if (arg == "--smoke-...")` 分支。另有 13 个 `*_smoke.h` 把实现写在头文件里。第一轮只是把它们从 `main.cpp` 原样搬出来。

**目标**：每个主题一个 `.cpp`，入口用表注册，新增自检不需要改分发函数。

**步骤**：

1. 定义 `struct SmokeEntry { const char* flag; int (*run)(const SmokeContext&); }` 和 `SmokeContext`（`argc`、`argv`、当前参数下标、`version`、`repoRoot`，以及 `valueAfter`、`hasFlag` 两个辅助函数）。
2. 按主题建文件：`smoke_file_browse.cpp`、`smoke_playback.cpp`、`smoke_analysis.cpp`、`smoke_record_export.cpp`、`smoke_video.cpp`、`smoke_device.cpp`、`smoke_separation.cpp`、`smoke_system_ui.cpp`。把每个分支体原样搬成一个函数，分支里用到的 `i` 改为 `context.index`。
3. `smoke_entries.cpp` 只剩一张注册表和一个查表循环。
4. 13 个 `*_smoke.h` 的 `inline` 实现移入对应主题的 `.cpp`。

**验证**：`tests/native` 全部通过即可，它们逐个调用这些入口。另外用脚本对比改动前后 `bf_app` 支持的 `--smoke-*` 开关集合完全一致。

**风险**：低。纯搬迁，但量大，适合分两三个 PR。

### A7 `bf_core` 按层拆成多个构建目标

**现状**：`bf_core` 一个静态库里装了 `core`、`ipc`、`license`、`i18n`、`render`、一批控制器，以及 `waveform_widget`、`video_panel`、`collapsible_panel` 三个控件，并公开链接 Fluent-Qt、Qt Widgets、Qt Multimedia、Qt Network。结果是：任何一个只测纯逻辑的测试都要链接全部界面依赖；构建系统也无法阻止下层引用上层。

**目标**：构建目标与目录分层一一对应，由 CMake 的链接关系强制分层。

**步骤**：

1. 拆为 `bf_foundation`（`core`、`i18n`）、`bf_ipc`、`bf_license`、`bf_render`、`bf_controller`、`bf_widgets`（三个共享控件）。每个目标只链接它允许依赖的下层目标和必需的 Qt 模块。
2. 保留一个名为 `bf_core` 的接口库把它们聚合起来，现有测试目标先不用改；之后再逐个把测试改成只链接需要的目标。
3. 做完后，`scripts/common/check_layering.py` 的检查保留作为快速反馈，CMake 链接关系作为最终约束。

**验证**：全量构建与回归；Windows 上确认各目标的导出与链接无误（静态库，风险不大）；对比拆分前后 `bf_app` 安装包内容不变。

**风险**：中。主要是 Windows/MSVC 下的链接顺序和 `AUTOMOC` 在多目标下的行为，放到 CI 上验证。

### A8 设备构建目标重新划分

**现状**：`bf_device_ui` 这个目标里混着设备控制器、TCP/Wi-Fi 采集源、原始录制、诊断日志，以及用 `../controller/live_ica_controller.*` 和 `../ui/device_workspace.*` 跨目录引用进来的两组文件。名字叫“device_ui”，内容大部分不是界面。

**目标**：`bf_acquisition_control`（`acquisition/control`、`sources`、`recording`）只含非界面代码；`live_ica_controller` 归 `bf_controller`；`device_workspace` 归 `bf_app` 或 `bf_widgets`。

**前置**：A7。

**风险**：低，只动 CMake。

### A9 原生测试统一注册并改按功能命名

**现状**：

- 35 个 `*_test.cpp` 与源码放在同一目录，主 `CMakeLists.txt` 里有 24 个测试可执行目标却没有一条 `add_test`（只有 `acquisition/CMakeLists.txt` 注册了 4 个）。这些测试靠 `tests/native/*.py` 逐个包装调用。
- 18 个测试和脚本按早已结束的里程碑命名（`test_m5_playback_smoke.py`、`test_m8_license_gate.py`、`verify_m1_foundation.sh`、`generate_m3_golden.py` 等），从名字看不出测的是什么。

**目标**：`ctest` 能跑全部原生单元测试；文件名说明功能。

**步骤**：

1. 给每个测试可执行目标补 `add_test`。pytest 包装保留，用于需要准备数据或检查输出的场景。
2. 按功能改名，例如 `test_m5_playback_smoke.py → test_playback_markers_smoke.py`、`test_m8_license_gate.py → test_license_gate_build.py`、`verify_m1_foundation.sh → verify_foundation.sh`、`m3_dsp_test → dsp_filter_test`、`m4_spectral_test → dsp_spectral_test`。改名时同步 CI 工作流、脚本、文档和 `AGENTS.md` 里的引用。
3. 是否把 `*_test.cpp` 移到 `native/tests/`：暂不建议。与源码同目录便于对照，移走的收益小于改动量。

**风险**：低，但改名波及面广，用 `git grep` 逐个确认引用。分两个 PR：先注册，再改名。

### A10 本机必然失败的三个测试改为按环境跳过

**现状**：在本机 WSL 环境下有三个测试必然失败，都与被测代码无关：

- `tests/native/test_m6_video_sync.py` 的两个真实媒体测试：测试用 ffmpeg 生成样本 MP4，本机 Conda 的 ffmpeg 没有 `libx264` 编码器，生成步骤返回码 8。测试已有“ffmpeg 不可用则跳过”的逻辑，但没有检测编码器。
- `tests/scripts/test_verify_script.py`：它调用 `scripts/wsl/verify_m1_foundation.sh --fast`。`--fast` 只是跳过 CMake 构建和界面自检，仍会执行 `pytest tests -q --ignore=tests/scripts`，也就是在一个测试里嵌套跑整套测试，包含最耗内存的授权门禁完整构建；并且默认到 `native/build-qt6-baseline` 找构建产物（可用 `BF_NATIVE_BUILD_DIR` 改），本机的外置构建目录不在那里。

每次本地回归都要人工解释“这几个失败是已知的”，也容易掩盖真正的新失败。

**步骤**：

1. 视频测试：把 `_ffmpeg_available()` 改为同时检查编码器，用 `ffmpeg -hide_banner -encoders` 的输出里是否有 `libx264` 和 `aac`；缺任何一个就 `pytest.skip` 并写明缺的是什么。函数上方那句“libx264/aac 在主流发行版默认启用”的注释同步改掉。
2. `test_verify_script.py`：脚本要找的构建目录里没有 `bf_app` 时跳过。再给脚本加一个环境变量开关（例如 `BF_VERIFY_SKIP_LICENSE_GATE=1`），设置后给内层 pytest 加 `--deselect tests/native/test_m8_license_gate.py`；这个测试自己调用脚本时设置该变量，因为授权门禁测试在外层 pytest 里本来就会单独跑一次，嵌套再跑是重复的。
3. 在 `docs/toolchain-wsl2.md` 写明本机回归的预期结果：全部通过或跳过，不应有失败。

**验证**：本机 `tests/native` 与 `tests/scripts` 结果为 0 失败；CI 上这三个测试仍然实际执行并通过（检查 CI 日志里它们是 passed 而不是 skipped）。

**风险**：低。唯一要防的是 CI 上也被跳过，所以第二条验证必须做。

## B. 文档

### B2 用户说明与开发入口分开

**现状**：`docs/analysis-guide.md` 和 `docs/device-acquisition.md` 把三类内容混在一起：给使用者的操作说明、给开发者的实现边界和回归入口、验收状态。`device-acquisition.md` 的原标题就是“候选能力与开发入口”。另外，`docs/quick-user-guide` 分支上有一份未提交的简明版 PDF 说明书，内容与这两份有重叠。

**目标**：使用者只需要读“怎么用”；开发者的内容在架构文档和契约里；验收状态只在验收清单里。

**步骤**：

1. 先处理 `docs/quick-user-guide` 分支：把它合入最新主线（`docs/README.md` 会冲突，把说明书那一行加到新索引的“使用软件”表里），提交并走 PR。确定 PDF 的来源文件放在哪里、以后怎么更新。
2. 逐节给两份文档的内容分类：操作说明留下；实现边界并入 `architecture-design.md` 对应的“行为约束”小节（去重）；回归入口并入架构文档或测试说明；验收状态并入 `feature-parity-checklist.md`。
3. 两份文档改名为 `user-guide-analysis.md`、`user-guide-device.md`（或合成一份 `user-guide.md`），更新索引和 README 的链接。
4. 决定 PDF 说明书与 Markdown 用户说明谁是真值，另一份由它生成或明确标注“以……为准”。

**风险**：中。分类时容易丢约束，逐节搬迁并在 PR 里列出“原第几节 → 去了哪里”的对照表。

### B3 发布 SOP 按提交 SOP 的方式精简

**现状**：`docs/release-sop.md` 182 行，常规路径与例外（重跑、分卷、Gitee 镜像、签名）写在同一条线上，和精简前的提交 SOP 是同一个问题。

**步骤**：开头放一页“发一个 beta 版”的常规命令序列；版本规则、安装包核验、Gitee 分卷同步、失败处理各成一节。节号变化后同步 `AGENTS.md`、提交 SOP、仓库技能和 Issue 模板里的引用（`release-sop.md#2-安装包下载与核验` 等锚点被多处引用，改之前先 `git grep 'release-sop.md#'`）。

**风险**：中低。发布流程实际只跑过一次（Beta1 草稿），精简时不要删掉那次实跑得到的注意事项。

### B4 契约文档拆分

**现状**：`docs/contracts.md` 407 行，包含版本规则、IPC 方法、数值与单位格式、错误码、翻译约定。

**步骤**：拆为 `contracts-ipc.md`（方法、帧格式、协议版本）和 `contracts-data.md`（数值格式、单位、时间轴、错误码与翻译键），原文件留作两者的索引或直接删除。只移动内容，不改契约。

**风险**：低。属于可做可不做，等有人抱怨找不到内容时再做。

### B5 工具链文档改名

**现状**：`docs/toolchain-wsl2.md` 同时讲 WSL2/Linux 和 Windows 本地构建，名字只提 WSL2。

**步骤**：改名为 `docs/toolchain.md`，更新全部引用（`AGENTS.md`、SOP、README、技能、脚本注释）。

## C. 流程与工具

### C1 文档类 PR 不跑完整构建

**现状**：分支保护要求两个 `build / package` 检查成功，且 PR 必须包含最新主线。一个只改文档的 PR 也要跑一轮约 25 分钟的双平台完整构建；主线每前进一次，所有打开的 PR 都要再跑一轮。2026-11-10 这次整理中，三个 PR 为此一共多等了三轮。

**目标**：只改 `docs/**`、`*.md` 的 PR 在两三分钟内得到同名的必需检查结果。

**步骤**：不能简单地给工作流加 `paths-ignore`，那样必需检查不会出现，PR 永远无法合入。现在的结构是 `ci.yml` 的 `build` 作业调用可复用工作流 `build-packages.yml`，后者的 `package` 作业按 `ubuntu-24.04`、`windows-2022` 展开，所以检查名是 `build / package (<系统>)`。要保持这两个名字不变：

1. 给 `build-packages.yml` 增加一个布尔输入 `docs_only`，默认 `false`。`release.yml` 也调用它，不传该输入，行为不变。
2. 在 `ci.yml` 里加一个前置作业，仅在 `pull_request` 事件下比较 PR 的改动文件列表：全部落在文档白名单（`docs/**`、根目录 `*.md`、`.agents/**`、`.github/*.md`、`.github/ISSUE_TEMPLATE/**`）内才输出 `docs_only=true`；`push` 和 `workflow_dispatch` 事件一律输出 `false`。
3. `build` 作业 `needs` 这个前置作业并把结果传入。`package` 作业里，安装依赖、构建、测试、打包、上传产物的步骤加 `if: ${{ !inputs.docs_only }}`；新增一组只在 `docs_only` 时运行的轻量步骤：`python3 scripts/common/version_sync.py check` 和文档链接校验。作业本身始终运行并正常结束，所以必需检查照常出现。
4. `master` 的 push 事件始终跑完整构建，保证主线内部包不受影响。

**验证**：开一个只改文档的测试 PR，确认两个必需检查在几分钟内成功且可合入；再开一个同时改文档和代码的 PR，确认仍跑完整构建。

**风险**：中。判断条件写错会让代码改动绕过构建，所以检测逻辑要保守：只要有一个文件不在文档白名单里，或者取不到改动列表，就跑完整构建。`version.md`、`version.json`、`README.md` 里的版本块受 `version_sync.py` 管理，改它们的 PR 属于版本准备，建议不算“只改文档”。工作流改动本身必须通过真实 PR 验证，并用 actionlint 检查。

### C2 仓库与工具的零散收尾

都是小事，可以攒在一个 PR 或顺手处理：

- **`docs/quick-user-guide` 分支**：见 B2 第 1 步。
- **已合入的旧远端分支**：GitHub 9 个、Gitee 3 个，此前按要求保留。确认不再需要后清理。
- **恢复点引用**：`refs/restore/20261104/ica-preserved-worktree`、`refs/restore/20261102/f-fluentqt`。确认内容已无用后删除。
- **根目录的 `license_public.pem`**：评估是否移到 `config/` 或 `native/resources/`，需同步 CMake 里的嵌入路径和发布资产检查。
- **`local-samples/`、`samples/`**：两个样本目录职责相近，确认后合并或在 README 写明区别。
- **Orca 项目登记**：Community 主目录在 Orca 里登记成了“文件夹项目”，不能为它新建工作树；这次是借同仓库的 `community-rtbci` 项目入口绕过去的。在 Orca 里把主目录改登记为 Git 项目。
- **pi 的目录信任**：pi 每进入一个新工作树都会询问是否信任，导致 Orca 派发超时。在 pi 里信任工作树的父目录一次即可。
- **工作树清理**：这次整理留下的 Orca 工作树 `docs-remove-archive`、`refactor-render-deps` 和外置构建目录下的 `community-restructure`，对应 PR 合入后删除。

## 不建议做的事

审查时考虑过、但判断为不值得的方向，记在这里避免重复讨论：

- **把 `render/` 改名为 `signal/`**：名字确实不够贴切（它装的是信号窗口和显示前处理，不是绘制代码），但 `acquisition/` 里已有 `bf_signal` 目标和 `model/signal_model`，改名会造成新的混淆，且波及上百处 include。在架构文档里写清它的实际职责即可。
- **把测试源码移到独立的 `native/tests/` 目录**：见 A9 第 3 步。
- **用插件机制重做设备适配**：目前只有三种来源（模拟器、BFSP TCP、RT-BCI Wi-Fi），`AcquisitionSource` 接口已经够用。等出现第四、第五种且有外部贡献者时再考虑。
- **重写 `MainWindow` 而不是逐步提取**：主界面行为多、隐性约束多，重写的回归风险远高于 A4 的小步提取。
