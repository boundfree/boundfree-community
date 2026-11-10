# AGENTS.md

`boundfree-community` 仓库的工作规则。代码代理和协作开发者修改本仓库前先读本文件。

本文件只写**规则和入口**，不写功能现状和进度。功能现状见 [路线](docs/roadmap.md)，未验收项见 [验收清单](docs/feature-parity-checklist.md)，提交记录见 Git。

## 这个仓库是什么

BoundFree EEG：脑电（EEG/LFP）信号的滤波、展示、分析与设备接入上位机。

- C++17 / Qt 6 原生主程序 `bf_app`，加一个负责科研文件读写的 Python 边车（`sidecar/`）。
- 当前版本为 `0.2.2-beta.1`，真值在 `version.json`。
- 自有核心采用 Apache-2.0。付费扩展、Pro 版和授权服务端在各自的私有仓库，不进入本仓库。

## 沟通

- 与用户沟通使用简体中文。
- 直接说明事实、改动、验证结果和剩余风险。
- 不把推测当结论。涉及构建、测试、性能或发布状态，先验证再说。

## 先读什么

| 任务 | 文档 |
| --- | --- |
| 找代码、判断新代码放哪里、理解数据流 | [docs/architecture-design.md](docs/architecture-design.md) |
| 建分支、提交、PR、合入 | [docs/contribution-sop.md](docs/contribution-sop.md) |
| 分支命名、提交格式、文档格式 | [docs/development-guide.md](docs/development-guide.md) |
| 本地构建与运行 | [docs/toolchain-wsl2.md](docs/toolchain-wsl2.md) |
| IPC、数值格式、错误码、翻译契约 | [docs/contracts.md](docs/contracts.md) |
| 设备接入、BFSP 协议 | [docs/device-acquisition.md](docs/device-acquisition.md)、[docs/bfsp-protocol.md](docs/bfsp-protocol.md) |
| 产品范围、现状、未验收项 | [docs/roadmap.md](docs/roadmap.md)、[docs/feature-parity-checklist.md](docs/feature-parity-checklist.md) |
| 版本、安装包、发布 | [docs/release-sop.md](docs/release-sop.md) |
| 许可与扩展边界 | [legal/LICENSING.md](legal/LICENSING.md) |

全部文档的索引在 [docs/README.md](docs/README.md)。

## 仓库地图

| 路径 | 内容 |
| --- | --- |
| `native/src/` | 主程序源码，分层见架构文档 |
| `sidecar/` | Python 边车：文件读取、录制、导出 |
| `contracts/` | 机器可读契约 |
| `tests/` | 回归测试（`native/`、`sidecar/`、`contracts/`、`scripts/`） |
| `scripts/` | 构建、版本和发布检查脚本 |
| `docs/` | 当前文档 |
| `installer/`、`release/`、`legal/` | 安装包、发布资源、许可文本 |

## 硬约束

架构：

- 上层依赖下层，下层不依赖上层：`app → ui → controller → render / ipc / license / i18n / acquisition → core / dsp`。
- `dsp/` 是纯数值代码，不依赖 Qt，也不依赖本仓库其他目录。
- 界面不解析文件、不实现算法、不解码协议。
- 主程序不直接解析科研文件格式，一律经 Python 边车。
- `main_window.cpp` 和 `main.cpp` 已经过大，新功能放独立的控制器或面板，不往这两个文件里加。

数据：

- 共享处理窗口形状为 `[n_channels, n_samples]`，逐通道带单位、时间坐标和来源。电压统一到 `uV`，原始 count 和其他物理量保留实际单位。
- 工具按采样率、连续性、单位和样本量判断能否使用，不按来源（文件或设备）整组禁用。
- 浏览与处理使用 float32 投影，原始录制保留 typed 数据，分离专用原文件入口保持 float64。
- IPC 使用 JSON 控制消息加长度前缀二进制帧，小端字节序。产品版本与 `ipc_protocol_version` 独立维护。

实现：

- 用户可见文字一律走 Qt i18n（`.ts/.qm`），不新增硬编码中文。
- 边车和后端只返回 `code + params`，不返回本地化文案。
- 滤波和 ICA 使用自有 C++ 实现，频谱使用 pocketfft。不引入 FFTW、KFR 等 GPL 风险依赖，不引入与闭源商业分发冲突的许可。
- 授权校验留在 C++ 原生侧，并兼容旧版密钥语义。
- Qt 基线由 `config/qt-version.txt` 管理，Python 基线由 `config/python-version.txt` 管理。
- 新增共享接口时，同步更新契约文档和对应测试。

## 交付流程

完整步骤见 [提交与交付 SOP](docs/contribution-sop.md)。代理同时加载仓库技能 [community-delivery](.agents/skills/community-delivery/SKILL.md)。要点：

1. 从最新 `origin/master` 建短分支，一个任务一个分支一个 PR。
2. 所有改动经 GitHub PR 合入 `master`，不直推。`master` 有分支保护，双平台 CI 必须通过。
3. 提交信息用中文 Conventional Commits：`type: 中文摘要`，type 为 `feat`、`fix`、`docs`、`test`、`chore`、`spike`、`refactor`、`perf`。
4. 普通提交不改产品版本。阶段变更按 [发布 SOP](docs/release-sop.md) 用 `scripts/common/version_sync.py`。
5. 合入后核对主线 CI，再把已验证的提交快进同步到 Gitee 镜像。

技能和 SOP 不扩大任务授权：只要求本地修改的任务，不自动提交、合入或发布。

## 验证

声称构建、测试或功能完成之前，必须运行对应命令并检查输出。

| 改动 | 至少运行 |
| --- | --- |
| 任何改动 | `git diff --check HEAD`、`python3 scripts/common/version_sync.py check` |
| C++ / Qt | CMake configure 与 build，加受影响的原生回归（命令见工具链文档） |
| Python 边车 | `python3 -m pytest tests/sidecar` |
| JSON | `python3 -m json.tool <file>` |
| 界面文字或翻译 | `python3 scripts/common/check_i18n.py` |
| 版本、发布脚本、许可 | `python3 scripts/common/check_release_assets.py` |

- 性能结论必须记录环境、命令、样本和结果。WSL 或 Xvfb 软件渲染的结果不能当作真实 GPU 验收。
- 模拟器、runner smoke 和合成数据的通过，不能当作真实设备、干净机或算法效果的验收。

## 本机环境

- 日常源码目录是 Linux 侧的 `/home/vsonic12138/workspace/BoundFree_Workspace/boundfree-community`。
- `F:\workspace\BoudFree\development\boundfree-community` 是从 Linux 单向同步的 Windows 编译副本，不在副本里编辑或提交。同步方法见工具链文档。
- **WSL2 内存约 12GiB。构建与测试并行度不超过 4**（`cmake --build <dir> --parallel 4`），禁止不带数字的 `-j`。否则会耗尽内存导致 WSL 重启。
- `tests/native/test_m8_license_gate.py` 会在临时目录完整编译 `bf_app`，最耗内存。
- 完整 pytest 按目录或文件分批串行运行，日志写到 `/mnt/boudfree-dev` 下（WSL 重启会清空 `/tmp`）。可用内存低于约 3GiB 时停止。

## 安全

- 不执行破坏性 Git 操作（`git reset --hard`、强推、删除用户改动），除非用户明确要求。
- 不提交构建产物、安装包、私钥、授权签发材料或临时环境目录。
- 不覆盖已发布的 tag 或同版本安装包。
- 不对 Apache-2.0 核心附加禁止商用、修改或再分发的限制。`legal/LICENSE.txt`、`legal/NOTICE.txt` 必须与根目录 `LICENSE`、`NOTICE` 一致。
- 不修改工作空间里的旧参考仓库 `archive/boundfree-eeg-upper-computer`，它只用于只读对照和黄金文件。

## 文档维护

- 每条规则只在一处展开，其他地方用链接指过去。
- 功能现状写进路线和验收清单，不写进本文件。
- 完成或被替代的计划、报告直接删除，靠 Git 历史追溯。删除前把仍有效的约束和未验收项并入当前文档。
- 流程规则变化时，在同一个 PR 里核对本文件、开发规范、两份 SOP、仓库技能和 PR 模板。
