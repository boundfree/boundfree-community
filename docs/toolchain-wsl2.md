---
title: 本地工具链与构建验证
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-23
---

# 本地工具链与构建验证

只想下载测试安装包时使用 [发布 SOP](release-sop.md) 的 Actions Artifacts，无需在 WSL 交叉编译。本文维护本地开发与实机验证入口，云端环境以 [.github/workflows/build-packages.yml](../.github/workflows/build-packages.yml) 为准；本地个人配置不能当作发布环境保证。

## WSL2 / Linux 开发

依赖 C++17、CMake 3.21+、Qt6 Core/Widgets/Multimedia/Network/LinguistTools；Linux 授权后端还需 OpenSSL。Python 边车依赖在 `sidecar/pyproject.toml`，测试文件格式需安装相应 formats 依赖。

现有 `wsl2-qt6-conda` preset 从 `native/../../toolchains/qt6` 查找 Qt 和 conda 编译器。当前工作空间该入口是指向 Qt/conda 环境的软链，其他开发者应按本机环境配置等价工具链，不照搬个人绝对路径。也可使用独立构建目录显式传入 `CMAKE_PREFIX_PATH`。

从仓库根目录构建：

```bash
cmake --preset wsl2-qt6-conda -S native
cmake --build native/build --parallel 4
```

或从 `native/` 目录执行开发规范中的两条 preset 命令。Qt、FFmpeg 和 Python 的实际版本应在验收时记录，不能用旧报告里的开发环境代替本轮环境。

基础验证从仓库根目录执行：

```bash
scripts/wsl/verify_m1_foundation.sh
```

`--fast` 仅用于已有构建的本地迭代，会跳过 CMake build 和 UI smoke，不是完整验收。Python/native 回归使用含所需格式依赖的解释器；使用 `BF_PYTHON_EXECUTABLE` 时明确它指向哪个环境。离屏回归可设置 `QT_QPA_PLATFORM=offscreen`，但它不提供真实显示/GPU结论。

## Windows 本地构建

WSL2 仓库是唯一源码；Windows 仓库仅用于 MSVC、Qt、安装包和真实桌面验证。首次在 WSL2 创建本机配置：

```bash
cp config/windows-dev.example.json config/windows-dev.local.json
```

编辑 `config/windows-dev.local.json` 填入 `wsl_sync_target` 和 `qt_root`。

从 WSL2 一键同步、构建、自动验收并把安装包复制回当前仓库：

```bash
./scripts/wsl/build_windows_dist.sh
```

`--fast` 会跳过 pip 依赖同步，并复用已有冻结边车；修改过 `sidecar/` 或依赖时请使用完整构建。`--no-sync` 仅适用于 Windows 副本已与当前源码和版本一致的情况。安装包回传到 WSL 仓库的 `dist/`，同时打印 SHA-256。

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
- 边车冷启动加载 MNE 可能触发 `ipc.sidecar_timeout`。记录首次打开与后续运行差异，检查冻结依赖、启动路径和超时配置；不能只凭热态重跑成功就宣称冷启动验收完成。
- Windows 工作副本整体改名或移动后，`native\build-windows` 里的 CMakeCache 仍指向老绝对路径，下次 configure 会失败；删除该目录重新配置即可（会触发一次全量构建）。

## GPU / 桌面验证

历史 64/128/256 通道、1 kHz、4 秒窗口的 GPU 基线保存在 [原验收记录](archive/reports/m2-rendering-gpu-gate.md)。需要对当前版本复测时，在真实显示环境使用现有采集脚本，先检查当前参数：

```bash
python3 scripts/common/collect_m2_gpu_gate_evidence.py --help
```

记录目标提交、OS、GPU/驱动、Qt、负载、FPS、CPU/GPU 观测来源以及滚动/缩放/通道显隐体验。WSL、Xvfb、Mesa 软件渲染或 offscreen 结果只作开发信号；历史 Passed 不自动代表当前版本与所有负载均通过。干净机、正式授权和发布验收仍按 [清单](feature-parity-checklist.md) 与 [发布 SOP](release-sop.md) 执行。
