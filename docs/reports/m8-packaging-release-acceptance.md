---
title: M8 Windows 打包与发布验收记录
doc_type: report
status: active
owner: BoundFree
last_updated: 2026-10-15
milestone: M8
---

# M8 Windows 打包与发布验收记录

## 结论

M8 打包实现与当前 WSL2 Linux 可验证部分已完成：冻结边车、Native 安装布局、安装目录资源定位、主题与系统 UI、离线授权实现、发布资产检查及全量回归通过。

M8 的 Windows 自动化部分已完成：2026-10-15 在 Windows x64 发布机执行 MSVC Release 构建、边车冻结、`windeployqt`、Inno Setup 安装包生成与仓库自动化验收（11 项 smoke 全通过），证据见「Windows 自动化验收执行记录」。组织证书代码签名、干净 Windows 10/11 安装与卸载、以及桌面全流程人工验收仍未完成；当前产物不得标为 `rc` 或 `stable`。

## 已实现范围

- `sidecar/boundfree_sidecar.spec` 生成 onedir 冻结边车并收集 MNE 动态依赖。
- Native 优先从可执行文件目录加载版本、错误码、i18n 和冻结边车，不依赖源码工作目录。
- CMake install 规则组装 Native、版本、错误码、i18n 与法务资源。
- `installer/BoundFreeEEG.iss` 定义 Windows 10 x64、自选安装目录、快捷方式、卸载和法务文件。
- `build_release.ps1` 固定 MSVC Release、边车冻结、CMake install、`windeployqt`、Inno Setup 与签名顺序；`rc/stable` 缺少签名配置时失败。
- Native 离线授权实现机器码兼容、RSA-2048 PSS-SHA256 校验、保存/加载与启动门禁；正式发布仍须注入实际公钥并执行旧版真实密钥兼容验收。
- 主窗口实现亮/暗主题、清华紫高亮、dock 显隐菜单、文件/视频/退出快捷键、常驻文件和模式状态。
- i18n 静态检查覆盖 `zh_CN/en_US/pseudo` 与保留 locale 的 key 完整性；后端错误保持 `code + params` 并由 UI 本地化。

### 授权时间水位补充回归（2026-08-12）

- `clock_rollback_test` 覆盖首次初始化、前进时间、300 秒容差边界、301 秒回拨拒绝、损坏状态保留、写入失败及在线授权后的状态恢复。
- `license_manager_clock_test` 在临时目录内运行时生成 RSA-2048 测试密钥，验证实际许可证保存链路：首次离线保存同时创建授权文件和时间水位；删除水位后启动与离线覆盖都失败且授权文件不变；在线保存可以恢复缺失或损坏的水位。
- 上述验证仅使用 WSL2/Linux 与临时密钥，不能替代 Windows 正式公钥、真实许可证、安装包和安全存储验收；Windows 发布门禁仍维持下列未勾选状态。

## 当前环境验证结果

```text
┌──────────────────────────┬──────────────────────────────────────────────┐
│ 场景                     │ 观察结果                                     │
├──────────────────────────┼──────────────────────────────────────────────┤
│ Native CMake build       │ PASS                                         │
│ 系统 UI 行为 smoke       │ 主题、dock、快捷键、状态标签 PASS            │
│ 冻结边车构建             │ PyInstaller onedir PASS，目录约 311 MiB      │
│ 冻结边车 CSV / EDF       │ open/get_slice PASS                          │
│ 冻结边车录制 / 导出      │ 双格式录制、三格式导出 PASS                  │
│ 安装布局                 │ CMake install PASS，Native stage 约 3.0 MiB  │
│ 脱离源码目录启动         │ --version 与 offscreen --smoke-ui PASS       │
│ 发布定向自动化           │ 43 passed                                    │
│ 全量 Python 测试         │ 235 passed                                   │
│ M3 / M4 黄金二进制       │ PASS                                         │
│ 授权实现 smoke           │ 25 项 PASS；启用门禁启动 PASS                │
│ i18n / JSON / 发布资产   │ PASS                                         │
└──────────────────────────┴──────────────────────────────────────────────┘
```

## 已执行命令

```text
cmake --preset wsl2-qt6-conda
cmake --build --preset wsl2-qt6-conda -j
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-m8-system-ui
python3 -m PyInstaller --noconfirm --clean --distpath ../release/sidecar-dist --workpath ../release/sidecar-build boundfree_sidecar.spec
BF_SIDECAR_EXECUTABLE=release/sidecar-dist/boundfree_sidecar/boundfree_sidecar native/build/bf_app --smoke-open-slice tests/fixtures/generated/simple_eeg.csv 0 0.05
BF_SIDECAR_EXECUTABLE=release/sidecar-dist/boundfree_sidecar/boundfree_sidecar native/build/bf_app --smoke-open-slice tests/fixtures/generated/simple_eeg.edf 0 0.05
BF_SIDECAR_EXECUTABLE=release/sidecar-dist/boundfree_sidecar/boundfree_sidecar native/build/bf_app --smoke-m7-record tests/fixtures/generated/simple_eeg.csv /tmp/bf-m8-record
BF_SIDECAR_EXECUTABLE=release/sidecar-dist/boundfree_sidecar/boundfree_sidecar native/build/bf_app --smoke-m7-export tests/fixtures/generated/simple_eeg.csv /tmp/bf-m8-export
cmake --install native/build --prefix release/stage-linux/BoundFreeEEG
PYTHONPATH=sidecar QT_QPA_PLATFORM=offscreen python3 -m pytest tests -q
native/build/m3_dsp_test tests/golden/m3
native/build/m4_spectral_test tests/golden/m4
python3 scripts/common/check_i18n.py
python3 scripts/common/check_release_assets.py
python3 -m json.tool contracts/error_codes.json
```

安装树从 `/tmp` 使用绝对路径执行 `bf_app --version` 和 `bf_app --smoke-ui --language en_US` 均成功，验证资源定位不依赖当前工作目录。

## Windows 自动化验收执行记录（2026-10-15）

执行机器：Windows x64 发布机。源码经 `scripts/wsl/sync_to_windows.sh` 从 WSL2 单向同步到 Windows 工作副本后，全程在 Windows PowerShell 5.1 下执行。

| 项 | 值 |
|---|---|
| 工具链 | Qt `E:\QT\6.8.2\msvc2022_64`、Visual Studio 2022 BuildTools（x64 Release）、CMake、Inno Setup 7（`E:\Program APP\Inno Setup 7\ISCC.exe`）、仓库 `.venv`（PyInstaller 6.21 + mne 1.12.1 + pyedflib/h5py/neo） |
| 版本 | `0.1.64-dev`（channel `dev`，未签名） |
| 安装包 | `dist\BoundFreeEEG_Setup_0.1.64-dev.exe`，88,069,369 字节 |
| SHA-256 | `c248ba89a281adfc8d5cfd695fd293a79dee6eb57e8d670355114a9c873530a6` |
| 暂存树 | `release\stage\BoundFreeEEG\`（bf_app.exe + Qt 运行时与插件 + `sidecar/`(含 mne) + i18n + contracts + legal） |
| 验收输出 | `release\windows-acceptance\20261015-192501\`（11 份 smoke 文本、`installer-sha256.txt`、`signature-status.json`、人工验收清单） |

执行命令：

```text
.\scripts\windows\build_release.ps1 -QtRoot "E:\QT\6.8.2\msvc2022_64" ^
  -Python "<repo>\.venv\Scripts\python.exe" -LicensePublicKeyFile license_public.pem ^
  -IsccPath "E:\Program APP\Inno Setup 7\ISCC.exe" -SkipSigning
.\scripts\windows\verify_windows_release.ps1
```

自动化 smoke 全部通过：`--version`、`--smoke-m8-system-ui`、`--smoke-sidecar`、`--smoke-license-gate`、CSV 与 EDF 的 `--smoke-open-slice`、`--smoke-processing-offline`、`--smoke-m5-playback`、`--smoke-m4-analysis`、`--smoke-m7-record`、`--smoke-m7-export`。本机热态实测：CSV 打开约 0.3 秒；EDF 首次打开约 2.2 秒（含 mne 导入）。

本次同时修正 `scripts/windows/verify_windows_release.ps1` 在出厂 Windows PowerShell 5.1 下无法运行的三处问题：文件缺少 UTF-8 BOM 导致中文 here-string 解析失败；`param()` 默认值引用 `$PSScriptRoot`（5.1 下为空）；`Invoke-Checked` 的 `2>&1` 会把原生命令 stderr 当致命错误，而系统 UI smoke 的诊断行走 stderr。修后该脚本在 Windows PowerShell 5.1 与 PowerShell 7 下都能跑完。

## Windows 发布门禁

`[x]` 表示已有验收证据，`[ ]` 表示仍待验收：

- [x] Visual Studio 2022 x64 Release 构建（2026-10-15，见上节）。
- [x] Inno Setup 安装包生成与 SHA-256 记录（安装/覆盖安装/卸载流程仍未测）。
- [ ] `windeployqt` 后在无 Qt/Python 开发环境的干净 Windows 10/11 用户上启动。当前包未部署 MSVC 运行时（`windeployqt` 报告 `VCINSTALLDIR` 未设置），干净机需先安装 VC++ 2015-2022 运行时。
- [ ] Inno Setup 安装、覆盖安装、卸载和自选安装目录。
- [ ] 使用正式公钥和旧版真实授权 key 完成激活兼容验收。
- [ ] 打开文件、回放、DSP、分析、视频、录制和三格式导出全流程桌面人工验收（自动化 smoke 已通过）。
- [ ] 使用组织证书签署 Native、冻结边车和安装包，并通过 `signtool verify /pa`（本次 dev 通道未签名）。
- [ ] 记录证书主题、时间戳服务、SmartScreen 结果和 Qt Multimedia/FFmpeg 实际分发许可证清单。

## 已知边界

- 历史证据来自 WSL2 Linux 与 Qt offscreen 环境；2026-10-15 的 Windows 自动化验收也仍是命令行 smoke（无真实桌面交互、显示、驱动与媒体后端验证）。
- Linux stage 不是交付物；仅用于验证 CMake install 布局与脱离源码目录运行。
- 开发环境 Qt Multimedia 报告 FFmpeg GPL 构建；Windows 发布前必须核对实际随包二进制及许可证，不能沿用开发环境结论。
- 真实组织代码签名证书、正式授权公钥和旧版真实授权样本不在仓库中，必须由发布环境安全注入。
