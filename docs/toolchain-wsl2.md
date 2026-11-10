---
title: 本地工具链与构建验证
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-11-04
---

# 本地工具链与构建验证

只想下载测试安装包时使用 [发布 SOP](release-sop.md) 的 Actions Artifacts，无需在 WSL 交叉编译。本文维护本地开发与实机验证入口，云端环境以 [.github/workflows/build-packages.yml](../.github/workflows/build-packages.yml) 为准；本地个人配置不能当作发布环境保证。

## WSL2 / Linux 开发

依赖 C++17、CMake 3.21+、Qt6 Core/Widgets/Multimedia/Network/LinguistTools；Linux 授权后端还需 OpenSSL。Python 边车依赖在 `sidecar/pyproject.toml`，测试文件格式需安装相应 formats 依赖。

Community 的共同 Qt 基线是 **6.8.2**，唯一版本配置为 `config/qt-version.txt`，CMake 默认精确校验，Windows/Linux CI 从同一文件读取版本。Windows 使用 MSVC 2022，WSL 使用 Conda GCC；统一 Qt 版本不表示二进制、媒体后端或系统依赖完全相同。

本机 WSL 是 Ubuntu 22.04，使用独立 Conda 环境，Qt/Python 前缀为 `/mnt/boudfree-dev/qt/6.8.2`，包含匹配的 Qt Multimedia、FFmpeg 7 LGPL 构建与 Python 3.12.12。Python 基线由 `config/python-version.txt` 管理，CI 同时读取 Qt/Python 两个版本文件；托管机器不一定提供每个安全补丁版的安装包（如 Windows 无 3.12.12），CI 按该文件的主次版本（3.12）安装，本机 Conda 环境保持精确版本。可按环境定义重建：

```bash
conda env create -f config/qt-wsl-environment.yml
# BF_QT_ROOT 可指向你自己的同版本安装；本工作空间已设置 toolchains/qt6-community。
export BF_QT_ROOT=/path/to/conda/envs/bf-qt6-6.8.2
scripts/wsl/with_qt.sh cmake --preset wsl2-qt6-conda -S native
scripts/wsl/with_qt.sh cmake --build native/build-qt6-baseline --parallel 3
scripts/wsl/with_qt.sh native/build-qt6-baseline/bf_app
```

`wsl2-qt6-conda` 预设使用 `native/cmake/wsl-qt6.cmake`，优先读取 `BF_QT_ROOT`，否则根据 Git common dir 查找工作空间的 `toolchains/qt6-community`，兼容主检出与独立工作树。新构建目录为 `native/build-qt6-baseline`；不要复用指向旧 Qt 的 CMakeCache。工具链与构建目录可通过 `BF_QT_ROOT`、`cmake -B` 覆盖。

`with_qt.sh` 仅为本次命令设置工具路径、运行库、插件目录和环境内 Python，避免混入其他 Qt 的插件。`toolchains/qt6` 与 `toolchains/qt6-community` 统一指向本机 6.8.2 环境。实验新 Qt 须使用独立构建目录和 `-DBF_ALLOW_OTHER_QT=ON`，日志会明确提示非基线版本。

本机唯一日常源码目录是 `/home/vsonic12138/workspace/BoundFree_Workspace/boundfree-community`，日常在 `master` 上建短分支，`.worktrees/community-ica` 兼容链接指向该 Linux 主目录。原保留工作树的未提交改动已备份到 `refs/restore/20261104/ica-preserved-worktree` 后移除。`F:\workspace\BoudFree\development\boundfree-community` 仅为 Windows 编译副本，不在其中修改源码、暂存或提交。

Windows Qt 位于 `F:\workspace\BoudFree\.dev\qt\windows\6.8.2\msvc2022_64`，Python 位于 `.dev\python\windows`。WSL 工具链与构建缓存仍放在 F 盘稀疏 ext4 镜像 `.dev/wsl/linux-dev.ext4`，挂载到 `/mnt/boudfree-dev`，由 `/etc/fstab` 自动挂载。源码恢复 Linux 不需要移动依赖或镜像。不要把编译缓存目录做成跨文件系统的软链接：Qt MOC 生成的相对 include 可能无法定位源码。本机忽略的 `native/CMakeUserPresets.json` 提供 `wsl2-linux` 预设，使用新目录 `/mnt/boudfree-dev/build/community-linux`，旧 F 盘源码对应的构建缓存保留，不复用其绝对路径：

```bash
cd /home/vsonic12138/workspace/BoundFree_Workspace/boundfree-community
scripts/wsl/with_qt.sh cmake --preset wsl2-linux -S native
scripts/wsl/with_qt.sh cmake --build /mnt/boudfree-dev/build/community-linux --parallel 4
scripts/wsl/with_qt.sh /mnt/boudfree-dev/build/community-linux/bf_app
BF_NATIVE_BUILD_DIR=/mnt/boudfree-dev/build/community-linux scripts/wsl/with_qt.sh scripts/wsl/verify_m1_foundation.sh
```

开发终端入口为 `F:\workspace\BoudFree\development\Windows-Dev.cmd` 与 `WSL-Dev.cmd`。旧 `E:\workspace` 兼容目录链接已移除，桌面 workspace 快捷方式已指向 F 盘，配置直接使用 F 路径。共享的 Visual Studio、Inno Setup 和 Conda 管理器保留原安装位置。挂载镜像的机器不应直接复制或移动正在使用的镜像文件。

Qt、FFmpeg、编译器与环境锁定清单应随构建证据保留。离屏回归可用 `QT_QPA_PLATFORM=offscreen`，不提供真实显示/GPU结论。

基础验证从仓库根目录执行：

```bash
scripts/wsl/verify_m1_foundation.sh
```

`--fast` 仅用于已有构建的本地迭代，会跳过 CMake build 和 UI smoke，不是完整验收。既有 Python native 测试仍按 `native/build` / `native/build-windows` 查找产物，尚未统一识别本机外置构建目录；应先检查各测试的查找规则。可直接运行 `/mnt/boudfree-dev/build/community-linux` 中的目标测试。历史记录保留了总入口因两个模块在缺少可发现二进制时使用模块级 `pytest.skip` 不当而失败的结果，不将该失败或未运行的测试计作通过。Python/native 回归使用含所需格式依赖的解释器；使用 `BF_PYTHON_EXECUTABLE` 时明确它指向哪个环境。离屏回归可设置 `QT_QPA_PLATFORM=offscreen`，但它不提供真实显示/GPU结论。

## Windows 本地构建

在 Linux/WSL 源码目录开发，通过受保护的单向同步更新 F 盘 Windows 编译副本，再在 Windows 使用 MSVC 构建。首次在 Linux 源码目录创建本机配置：

```bash
cp config/windows-dev.example.json config/windows-dev.local.json
```

编辑 `config/windows-dev.local.json` 填入 `wsl_sync_target` 和 `qt_root`。本机同步目标为 `/mnt/f/workspace/BoudFree/development/boundfree-community`，不使用旧 E 盘路径。默认通过 Python 3.12 启动器创建 `.venv`；本机以 `python_executable` 直接指定 F 盘 Conda Python，同时将 `bootstrap_python` 指向该解释器、`bootstrap_python_args` 设为空数组。两端的本机配置和 `native/CMakeUserPresets.json` 各自保留，不通过源码同步覆盖。

从 WSL2 一键同步、构建、自动验收并把安装包复制回当前仓库：

```bash
./scripts/wsl/build_windows_dist.sh
```

`--fast` 会跳过 pip 依赖同步，并复用已有冻结边车；修改过 `sidecar/` 或依赖时请使用完整构建。`--no-sync` 仅适用于 Windows 副本已与当前源码和版本一致的情况。安装包回传到 WSL 仓库的 `dist/`，同时打印 SHA-256。

从 Linux 源码目录预览并单向同步到已登记的 Windows 编译副本：

```bash
./scripts/wsl/sync_to_windows.sh --dry-run
./scripts/wsl/sync_to_windows.sh
```

首次目标必须是空目录或新目录，追加 `--init` 初始化；已有非空、未登记副本先备份并归集修改，再使用新的空目录，不能直接覆盖：

```bash
./scripts/wsl/sync_to_windows.sh --init
```

同步清单 `.boundfree-windows-mirror.json` 记录源目录、提交基准和逐文件 SHA256，覆盖未提交源码。同步前检查目标的修改、删除和额外源码文件；发现差异立即停止，先把独有修改归集到 Linux。镜像不能反向作为源码，也不能从另一份旧源码覆盖。清单、并发锁、本机配置、私有材料、构建缓存与打包产物不入源码同步。`--dry-run` 只检查并列出变更，不写文件；复制失败会回滚本次触及的源码。首次恢复现有 F 盘副本的清单由经过备份与摘要校验的迁移步骤建立，普通同步不提供强制覆盖开关。

随后在 Windows 10/11 的同步仓库根目录运行：

```powershell
.\scripts\windows\build_windows.ps1
```

该入口依次执行 MSVC Release 构建、冻结边车、CMake install、`windeployqt`、Inno Setup 安装包，并在最后调用验收脚本；也可分开执行：

```powershell
.\scripts\windows\build_release.ps1 -QtRoot "F:\workspace\BoudFree\.dev\qt\windows\6.8.2\msvc2022_64" -LicensePublicKeyFile license_public.pem
.\scripts\windows\verify_windows_release.ps1
```

产物位置：安装包在 Windows 仓库 `dist\BoundFreeEEG_Setup_<版本>.exe`，暂存树在 `release\stage\BoundFreeEEG\`，验收输出（各 smoke 文本、`installer-sha256.txt`、签名状态、人工验收清单）在 `release\windows-acceptance\<时间戳>\`。

执行 Windows 侧步骤时的已知坑：

- 验收脚本同时支持出厂 Windows PowerShell 5.1 与 PowerShell 7；脚本文件带 UTF-8 BOM，改动时不要去掉（5.1 会把中文 here-string 读坏），`param()` 默认值内也不要引用 `$PSScriptRoot`（5.1 下为空）。
- 不要从 WSL 用相对路径或 Linux 风格路径启动 Windows 侧 `bf_app.exe`。`VersionService::detectRepoRoot(argv[0])` 在解析不到应用目录时会回退到当前工作目录，于是资源根变成 WSL/源码目录：边车会退化为 `python -m boundfree_sidecar`（本机命中 `E:\miniconda3`，没有 mne），EDF/BDF/BrainVision/EEGLAB/FIF 会报 `reader.open_failed`。用完整 Windows 路径（`& 'F:\...\release\stage\BoundFreeEEG\bf_app.exe'`）或在 PowerShell 里调用。
- 边车冷启动加载 MNE 可能触发 `ipc.sidecar_timeout`。记录首次打开与后续运行差异，检查冻结依赖、启动路径和超时配置；不能只凭热态重跑成功就宣称冷启动验收完成。
- Windows 工作副本整体改名或移动后，`native\build-windows` 里的 CMakeCache 仍指向老绝对路径，下次 configure 会失败；删除该目录重新配置即可（会触发一次全量构建）。

## GPU / 桌面验证

历史 64/128/256 通道、1 kHz、4 秒窗口的 GPU 基线见 Git 历史（提交 `1c55f6c` 的 `docs/archive/reports/m2-rendering-gpu-gate.md`）。需要对当前版本复测时，在真实显示环境使用现有采集脚本，先检查当前参数：

```bash
python3 scripts/common/collect_m2_gpu_gate_evidence.py --help
```

记录目标提交、OS、GPU/驱动、Qt、负载、FPS、CPU/GPU 观测来源以及滚动/缩放/通道显隐体验。WSL、Xvfb、Mesa 软件渲染或 offscreen 结果只作开发信号；历史 Passed 不自动代表当前版本与所有负载均通过。干净机、正式授权和发布验收仍按 [清单](feature-parity-checklist.md) 与 [发布 SOP](release-sop.md) 执行。
