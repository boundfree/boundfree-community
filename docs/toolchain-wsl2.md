---
title: 本地工具链与构建验证
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-11-10
---

# 本地工具链与构建验证

只想下载测试安装包时使用 [发布 SOP](release-sop.md) 的 Actions Artifacts，无需在 WSL 交叉编译。本文维护本地开发与实机验证入口，云端环境以 [.github/workflows/build-packages.yml](../.github/workflows/build-packages.yml) 为准；本地个人配置不能当作发布环境保证。

## WSL2 / Linux 开发

依赖 C++17、CMake 3.21+、Qt6 Core/Widgets/Multimedia/Network/LinguistTools；Linux 授权后端还需 OpenSSL。Python 边车依赖在 `sidecar/pyproject.toml`，测试文件格式需安装相应 formats 依赖。

Community 的共同 Qt 基线是 **6.8.2**，唯一版本配置为 `config/qt-version.txt`，CMake 默认精确校验，Windows/Linux CI 从同一文件读取版本。Windows 使用 MSVC 2022，WSL 使用 Conda GCC；统一 Qt 版本不表示二进制、媒体后端或系统依赖完全相同。

Linux 侧推荐用独立 Conda 环境提供完整工具链：Qt 6.8.2（含匹配的 Qt Multimedia、FFmpeg 7 LGPL 构建）和基线 Python。环境定义在 `config/qt-wsl-environment.yml`，没有现成环境时按它重建。Python 基线由 `config/python-version.txt` 管理，CI 同时读取 Qt/Python 两个版本文件；托管机器不一定提供每个安全补丁版的安装包（如 Windows 无 3.12.12），CI 按该文件的主次版本（3.12）安装，本地 Conda 环境可以保持精确版本。

下面的路径变量都换成各自的路径，构建目录可以放在仓库外：

```bash
export BF_QT_ROOT=/path/to/conda/envs/bf-qt6-6.8.2   # 必须是 6.8.2 的安装
cd /path/to/boundfree-community                       # 仓库根目录，下面的相对路径都从这里算
conda env create -f config/qt-wsl-environment.yml
export BF_BUILD_DIR=/path/to/community-linux-build    # 构建目录，可在仓库外
scripts/wsl/with_qt.sh cmake --preset wsl2-qt6-conda -B "$BF_BUILD_DIR" -S native
scripts/wsl/with_qt.sh cmake --build "$BF_BUILD_DIR" --parallel 4
scripts/wsl/with_qt.sh "$BF_BUILD_DIR/bf_app"
BF_NATIVE_BUILD_DIR="$BF_BUILD_DIR" scripts/wsl/with_qt.sh scripts/wsl/verify_m1_foundation.sh
```

最后一行是完整验证入口：它会重新配置与构建、跑整套 pytest 并做界面自检，内存与并行度按 [AGENTS.md](../AGENTS.md) 的资源规则执行。新环境按 `config/qt-wsl-environment.yml` 只提供 Qt 与基线 Python，文件格式测试还需要 `sidecar/pyproject.toml` 里的 formats 依赖。

`wsl2-qt6-conda` 预设使用 `native/cmake/wsl-qt6.cmake`，优先读取 `BF_QT_ROOT`，否则根据 Git common dir 查找工作空间的 `toolchains/qt6-community`，兼容主检出与独立工作树。预设默认构建目录是 `native/build-qt6-baseline`，用 `cmake --preset ... -B <build-dir>` 改到别处；不要复用指向旧 Qt 的 CMakeCache。

`with_qt.sh` 仅为本次命令设置工具路径、运行库、插件目录和环境内 Python，避免混入其他 Qt 的插件。`toolchains/qt6` 与 `toolchains/qt6-community` 统一指向同一个 6.8.2 环境。实验新 Qt 须使用独立构建目录和 `-DBF_ALLOW_OTHER_QT=ON`，日志会明确提示非基线版本。

源码在 Linux/WSL 侧编辑，Windows 只接收单向同步的编译副本，不在副本里修改源码、暂存或提交，同步与清单保护见下文。工具链和构建缓存可以放在仓库外或单独挂载的磁盘上，位置由 `BF_QT_ROOT` 和构建目录参数决定；构建缓存放在单独挂载的镜像或磁盘上时，不要在挂载期间复制或移动正在使用的镜像文件。不要把构建缓存目录做成跨文件系统的软链接，Qt MOC 生成的相对 include 可能无法定位源码。`native/CMakeUserPresets.json` 是被忽略的机器本地预设文件，不随源码同步。

Qt、FFmpeg、编译器与环境锁定清单应随构建证据保留。离屏回归可用 `QT_QPA_PLATFORM=offscreen`，不提供真实显示/GPU结论。

基础验证从仓库根目录执行：

```bash
scripts/wsl/verify_m1_foundation.sh
```

`--fast` 仅用于已有构建的本地迭代，会跳过 CMake build 和 UI smoke，不是完整验收。外置构建目录没有统一的覆盖方式：`scripts/wsl/verify_m1_foundation.sh` 读 `BF_NATIVE_BUILD_DIR`（默认 `native/build-qt6-baseline`），但它只是总入口自己的产物目录；`tests/native` 里各测试各自决定查找位置，多数只认 `native/build` / `native/build-windows`，只有个别测试（如 `test_bfsp_native.py`）有自己的变量。设置 `BF_NATIVE_BUILD_DIR` 不会让这些 pytest 改到外置目录，构建目录在别处时直接运行该目录里的目标测试，并先看对应测试的查找规则。历史记录保留了总入口因两个模块在缺少可发现二进制时使用模块级 `pytest.skip` 不当而失败的结果，不将该失败或未运行的测试计作通过。Python/native 回归使用含所需格式依赖的解释器；使用 `BF_PYTHON_EXECUTABLE` 时明确它指向哪个环境。离屏回归可设置 `QT_QPA_PLATFORM=offscreen`，但它不提供真实显示/GPU结论。

## Windows 本地构建

在 Linux/WSL 源码目录开发，通过受保护的单向同步更新 Windows 编译副本，再在 Windows 使用 MSVC 构建。首次在 Linux 源码目录创建本机配置：

```bash
cp config/windows-dev.example.json config/windows-dev.local.json
```

编辑 `config/windows-dev.local.json` 填入两项：`wsl_sync_target` 是 Windows 编译副本在 WSL 下的路径（例如 `/mnt/d/dev/boundfree-community`），`qt_root` 是该机 Qt 6.8.2 MSVC 安装目录（例如 `C:\Qt\6.8.2\msvc2022_64`）。示例文件里的路径只是格式示例，必须改成自己的实际路径，其他字段按需调整。默认通过 Python 3.12 启动器创建 `.venv`；也可用可选的 `python_executable` 直接指定解释器，此时把 `bootstrap_python` 指向同一解释器、`bootstrap_python_args` 设为空数组。两端的本机配置和 `native/CMakeUserPresets.json` 各自保留，不通过源码同步覆盖。

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

同步清单 `.boundfree-windows-mirror.json` 记录源目录、提交基准和逐文件 SHA256，覆盖未提交源码。同步前检查目标的修改、删除和额外源码文件；发现差异立即停止，先把独有修改归集到 Linux。镜像不能反向作为源码，也不能从另一份旧源码覆盖。清单、并发锁、本机配置、私有材料、构建缓存与打包产物不入源码同步。`--dry-run` 只检查并列出变更，不写文件；复制失败会回滚本次触及的源码。给已存在的副本首次登记清单时先备份并核对摘要，普通同步不提供强制覆盖开关。

随后在 Windows 10/11 的同步仓库根目录运行：

```powershell
.\scripts\windows\build_windows.ps1
```

该入口依次执行 MSVC Release 构建、冻结边车、CMake install、`windeployqt`、Inno Setup 安装包，并在最后调用验收脚本；也可分开执行：

```powershell
.\scripts\windows\build_release.ps1 -QtRoot "C:\Qt\6.8.2\msvc2022_64" -LicensePublicKeyFile license_public.pem
.\scripts\windows\verify_windows_release.ps1
```

产物位置：安装包在 Windows 仓库 `dist\BoundFreeEEG_Setup_<版本>.exe`，暂存树在 `release\stage\BoundFreeEEG\`，验收输出（各 smoke 文本、`installer-sha256.txt`、签名状态、人工验收清单）在 `release\windows-acceptance\<时间戳>\`。

执行 Windows 侧步骤时的已知坑：

- 验收脚本同时支持出厂 Windows PowerShell 5.1 与 PowerShell 7；脚本文件带 UTF-8 BOM，改动时不要去掉（5.1 会把中文 here-string 读坏），`param()` 默认值内也不要引用 `$PSScriptRoot`（5.1 下为空）。
- 不要从 WSL 用相对路径或 Linux 风格路径启动 Windows 侧 `bf_app.exe`。`VersionService::detectRepoRoot(argv[0])` 在解析不到应用目录时会回退到当前工作目录，于是资源根变成 WSL/源码目录：边车会退化为 `python -m boundfree_sidecar`，命中的解释器没有装 MNE 依赖时，EDF/BDF/BrainVision/EEGLAB/FIF 会报 `reader.open_failed`。用完整 Windows 路径（`& 'D:\dev\boundfree-community\release\stage\BoundFreeEEG\bf_app.exe'`）或在 PowerShell 里调用。
- 边车冷启动加载 MNE 可能触发 `ipc.sidecar_timeout`。记录首次打开与后续运行差异，检查冻结依赖、启动路径和超时配置；不能只凭热态重跑成功就宣称冷启动验收完成。
- Windows 工作副本整体改名或移动后，`native\build-windows` 里的 CMakeCache 仍指向老绝对路径，下次 configure 会失败；删除该目录重新配置即可（会触发一次全量构建）。

## GPU / 桌面验证

历史 64/128/256 通道、1 kHz、4 秒窗口的 GPU 基线见 Git 历史（提交 `1fe5cec` 的 `docs/archive/reports/m2-rendering-gpu-gate.md`）。需要对当前版本复测时，在真实显示环境使用现有采集脚本，先检查当前参数：

```bash
python3 scripts/common/collect_m2_gpu_gate_evidence.py --help
```

记录目标提交、OS、GPU/驱动、Qt、负载、FPS、CPU/GPU 观测来源以及滚动/缩放/通道显隐体验。WSL、Xvfb、Mesa 软件渲染或 offscreen 结果只作开发信号；历史 Passed 不自动代表当前版本与所有负载均通过。干净机、正式授权和发布验收仍按 [清单](feature-parity-checklist.md) 与 [发布 SOP](release-sop.md) 执行。
