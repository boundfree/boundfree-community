# BoundFree EEG Native

BoundFree EEG Native 是 BoundFree 脑电上位机的 C++/Qt6 原生重写项目。新架构采用原生主程序承载 UI、渲染、DSP、授权和国际化，用 Python 编解码边车保留 MNE、pyedflib、h5py、pandas 等科研文件格式能力。

## Current Status

当前版本源：[version.json](version.json)

```json
{
  "schema_version": 1,
  "product_name": "BoundFree EEG",
  "version": "0.1.69-dev",
  "release_channel": "dev",
  "ipc_protocol_version": 1
}
```

当前阶段：M0–M7 均已完成；M8 打包实现、WSL2 Linux 可验证部分与 Windows x64 安装包自动化验收已完成（2026-10-15），组织证书签名、干净 Windows 10/11 安装与桌面全流程人工验收待执行。产品版本 `0.1.69-dev`。

产品分线：BoundFree EEG 按信号范式分为共享同一套底座的两条产品线。**第一版本（v1）只交付「公共底座 + EEG 分析线」**，即面向动物电生理科研场景的通用 EEG/LFP/电生理文件分析软件；v1 的主线是完整迁移旧 Python/PySide6 版的文件打开、懒加载、波形浏览、DSP、频谱分析、事件标注、视频同步、片段导出、录制与 Session 骨架、离线授权和 Windows 发布能力。

高频 **Spike 分析线**（spike sorting、raster/PSTH 等）作为下一阶段的独立产品面孔，复用同一底座接入，不重做一套软件。自研 `ADS1299-4 + ESP32-C3`、BLE 实时采集、真实设备录制、设备状态监控和 12 小时采集长稳测试不作为 v1 stable 门槛，进入 v1.1/v2。v1 仅保留数据源、Session、录制、实时管线、分析层与渲染层的扩展边界。当前 Spike 线已完成**底座打通原型**（`bf_core` 共享库、`bf_spike` 可执行、检测/sorting DSP、分析服务与 4 个可视化视图），仅以 30 kHz 合成信号验证 `fs` 全程来自 metadata；真实 Intan 样本回归、sorting 黄金基线与视图性能门禁尚未建立，详见 [`docs/reports/spike-foundation-acceptance.md`](docs/reports/spike-foundation-acceptance.md)。

M0 结论：

- IPC 已进入并支撑 M2 文件 `open/get_slice` 路径：stdio + JSON 控制消息 + 长度前缀二进制 payload。
- 真实 Windows GPU/显示验收已通过；记录见 [`docs/reports/m2-rendering-gpu-gate.md`](docs/reports/m2-rendering-gpu-gate.md)。
- 旧 Python 版继续作为参考实现、性能基线以及 M3/M4 黄金文件来源。

## Current Milestone

M8 正在发布验收：实现与当前环境验证见 [`docs/reports/m8-packaging-release-acceptance.md`](docs/reports/m8-packaging-release-acceptance.md)。Windows x64 安装包已生成并通过仓库自动化验收（版本、系统 UI、边车握手、离线授权门禁、CSV/EDF 打开切片、离线 DSP、回放、频谱分析、录制与导出共 11 项）；组织证书签名、干净 Windows 10/11 安装与卸载、桌面全流程人工验收仍未完成，当前产物不得标为 `rc` 或 `stable`。

已完成阶段的可验证记录：

- [`docs/reports/m2-format-acceptance.md`](docs/reports/m2-format-acceptance.md)：已验收格式与未实现格式。
- [`docs/reports/m2-rendering-gpu-gate.md`](docs/reports/m2-rendering-gpu-gate.md)：真实 GPU/显示门禁。
- [`docs/reports/m3-dsp-acceptance.md`](docs/reports/m3-dsp-acceptance.md)：DSP 黄金比对、离线处理接入和测试命令。
- [`docs/reports/m4-spectral-acceptance.md`](docs/reports/m4-spectral-acceptance.md)：频谱算法、后台调度、渲染和黄金比对。
- [`docs/reports/m5-playback-annotations-acceptance.md`](docs/reports/m5-playback-annotations-acceptance.md)：回放状态、窗口预取、事件标注和 CSV 往返。
- [`docs/reports/m6-video-sync-acceptance.md`](docs/reports/m6-video-sync-acceptance.md)：视频容器、从属时钟、偏移、黑屏、倍速和帧节流。
- [`docs/reports/m7-record-export-acceptance.md`](docs/reports/m7-record-export-acceptance.md)：二进制 IPC、双格式录制、背压、三格式导出和回读。
- [`docs/reports/m8-packaging-release-acceptance.md`](docs/reports/m8-packaging-release-acceptance.md)：打包实现、Linux 可验证证据、Windows 自动化验收执行记录与未完成发布门禁。
- [`docs/reports/spike-foundation-acceptance.md`](docs/reports/spike-foundation-acceptance.md)：Spike 分析线底座原型、复用性核验与未完成门禁。

## Documentation

日常开发优先阅读：

| 文档 | 用途 |
| --- | --- |
| [docs/roadmap.md](docs/roadmap.md) | v1 边界、当前阶段、M0–M8 路线和门禁 |
| [docs/plans/m8-packaging-release.md](docs/plans/m8-packaging-release.md) | 当前 M8 打包实现、Windows 发布门禁与验收条件 |
| [docs/contracts.md](docs/contracts.md) | 版本、IPC、错误码、i18n 基础契约 |
| [docs/development-guide.md](docs/development-guide.md) | 分支、版本、文档和验证规范 |
| [docs/architecture-design.md](docs/architecture-design.md) | 长期架构设计 |
| [docs/feature-parity-checklist.md](docs/feature-parity-checklist.md) | 新旧版本功能对齐验收清单 |

历史计划和旧规范已归档到 [docs/archive/](docs/archive/)。

## Repository Layout

```text
boundfree-community/
├── AGENTS.md                    # 仓库级开发指导
├── README.md                    # 项目入口说明
├── version.json                 # 产品版本与 IPC 协议版本真值
├── version.md                   # 提交级版本记录
├── docs/                        # 当前核心文档与历史归档
├── native/                      # C++/Qt6 主程序
├── sidecar/                     # Python 编解码边车
└── tests/                       # 黄金文件、回归和冒烟测试
```

## Branch And Version Model

- 日常开发基于 `master`。
- 当前开发以已完成的 M1–M8 能力为基础，按当前里程碑从 `master` 创建 `feature/*`、`fix/*`、`spike/*` 或 `docs/*` 分支。
- `release` 是稳定发布占位分支，首个 `rc` 或 `stable` 版本前不打正式 tag。
- `version.json` 是产品版本真值；根目录 [`version.md`](version.md) 从版本同步规范启用提交开始记录每次有效提交的版本、日期、作者、提交标题和提交正文。
- 每个合入 `master` 的有效提交递增版本并更新 `version.md`；提交前运行 `python3 scripts/common/version_sync.py check`。
- 产品版本和 `ipc_protocol_version` 分开维护。

## Windows Dev Build

WSL2 仓库是唯一源码；Windows 仓库仅用于 MSVC、Qt、安装包和真实桌面验证。首次在 WSL2 创建本机配置：

```bash
cp config/windows-dev.example.json config/windows-dev.local.json
```

编辑 `config/windows-dev.local.json` 填入 `wsl_sync_target` 和 `qt_root`。

从 WSL2 单向同步到已存在的 Windows 验证副本：

```bash
./scripts/wsl/sync_to_windows.sh
```

目标目录为空目录或全新建时，追加 `--init` 自动初始化并跳过 `version.json` 校验：

```bash
./scripts/wsl/sync_to_windows.sh --init
```

随后在 Windows 10/11 的同步仓库根目录运行：

```powershell
.\scripts\windows\build_windows.ps1
```

该入口依次执行 MSVC Release 构建、冻结边车、CMake install、`windeployqt`、Inno Setup 安装包，并在最后调用验收脚本；也可分开执行：

```powershell
.\scripts\windows\build_release.ps1 -QtRoot "E:\Qt\6.8.2\msvc2022_64" -LicensePublicKeyFile license_public.pem
.\scripts\windows\verify_windows_release.ps1
```

产物位置：安装包在 Windows 仓库 `dist\BoundFreeEEG_Setup_<版本>.exe`，暂存树在 `release\stage\BoundFreeEEG\`，验收输出（各 smoke 文本、`installer-sha256.txt`、签名状态、人工验收清单）在 `release\windows-acceptance\<时间戳>\`。

执行 Windows 侧步骤时的已知坑：

- 验收脚本同时支持出厂 Windows PowerShell 5.1 与 PowerShell 7；脚本文件带 UTF-8 BOM，改动时不要去掉（5.1 会把中文 here-string 读坏），`param()` 默认值内也不要引用 `$PSScriptRoot`（5.1 下为空）。
- 不要从 WSL 用相对路径或 Linux 风格路径启动 Windows 侧 `bf_app.exe`。`VersionService::detectRepoRoot(argv[0])` 在解析不到应用目录时会回退到当前工作目录，于是资源根变成 WSL/源码目录：边车会退化为 `python -m boundfree_sidecar`（本机命中 `E:\miniconda3`，没有 mne），EDF/BDF/BrainVision/EEGLAB/FIF 会报 `reader.open_failed`。用完整 Windows 路径（`& 'E:\...\release\stage\BoundFreeEEG\bf_app.exe'`）或在 PowerShell 里调用。
- 边车冷启动首次打开 MNE 格式文件要 import mne，可能超过 `SidecarClient` 的 3 秒 `open` 超时并报 `ipc.sidecar_timeout`；重跑验收脚本即可通过（热态首次约 2.2 秒）。
- Windows 工作副本整体改名或移动后，`native\build-windows` 里的 CMakeCache 仍指向老绝对路径，下次 configure 会失败；删除该目录重新配置即可（会触发一次全量构建）。
