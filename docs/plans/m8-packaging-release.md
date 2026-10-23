---
title: M8 Windows 打包与发布验收计划
doc_type: plan
status: active
owner: BoundFree
last_updated: 2026-10-23
milestone: M8
---

# M8 Windows 打包与发布验收计划

## Objective

把 C++/Qt6 主程序、Python 编解码边车、Qt 运行时、版本/错误/i18n 资源与法务材料组装为可复现的 Windows x64 安装包，并建立代码签名和干净机验收门禁。

## Current State

云端构建与版本管理已扩展为 Windows / Ubuntu 双平台流水线，操作见 [发布 SOP](../release-sop.md)，实际验证结果见 [云端流程验收](../reports/community-release-automation.md)。下面保留 M8 原始发布范围；Ubuntu 归档属于后续构建扩展。

- M1–M7 已完成；M8 打包代码、系统 UI 收口与离线授权实现已落地。
- Native 与边车均支持安装目录资源定位；冻结边车、CMake install 布局和脱离源码工作目录启动已在 WSL2 Linux 验证。
- PyInstaller spec、`windeployqt` 流水线、Inno Setup 脚本、法务资产检查及 `rc/stable` 签名门禁已实现。
- 离线授权已实现机器码兼容、RSA-2048 PSS-SHA256 校验、保存/加载与启动门禁；固定向量通过，仍待正式公钥和旧版真实 key 兼容验收。
- 当前环境是 WSL2 Linux，没有 MSVC、Windows Qt、Inno Setup、`signtool` 或代码签名证书。Windows 安装、签名、SmartScreen 与真实 GUI 全流程必须在 Windows 发布机完成，见 `../reports/m8-packaging-release-acceptance.md`。

## Architecture

```text
源码树
  │
  ├─► CMake MSVC Release ─► bf_app.exe ─► windeployqt
  │
  ├─► PyInstaller spec ───► sidecar/boundfree_sidecar.exe
  │
  ├─► 版本 / i18n / contracts / legal
  │
  ▼
release/stage/BoundFreeEEG/
  ├─ bf_app.exe + Qt DLL/plugins
  ├─ sidecar/boundfree_sidecar.exe
  ├─ version.json
  ├─ contracts/error_codes.json
  ├─ i18n/bf_app_*.ts
  └─ legal/
       │
       ▼
Inno Setup ─► BoundFreeEEG_Setup_<version>.exe ─► signtool
```

## Changes

1. 添加 `sidecar/boundfree_sidecar.spec`，使用 `collect_all('mne')` 收集 MNE，显式包含版本和错误码资源，并保留 h5py/pyedflib/numpy。
2. 调整边车版本/错误码资源定位，开发态读取仓库根，冻结态读取可执行目录内资源。
3. 调整 `SidecarClient`：优先使用环境变量，其次使用安装目录 `sidecar/boundfree_sidecar.exe`，最后回退开发态 Python 模块。
4. 调整 Native 资源根定位：安装目录优先支持 `version.json/contracts/i18n`，不再要求源码树层级。
5. 添加 CMake install 规则，定义 Native Release staging 结构。
6. 添加 `installer/BoundFreeEEG.iss`，要求 `DisableDirPage=no`、Windows 10 x64、法务文件、快捷方式与卸载。
7. 添加 `build_release.ps1`，顺序执行 MSVC Release、PyInstaller、CMake install、`windeployqt`、Inno Setup 和可选 `signtool`。
8. 添加发布资产校验脚本：验证版本一致性、资源齐全、禁止私钥/keygen、spec 包含 `collect_all('mne')`、安装脚本包含法务材料。
9. 增加 Native 系统级收口：主题、dock 可见性菜单、状态栏文件/模式、快捷键和离线授权迁移按功能清单逐项完成。
10. Windows 发布机执行干净用户安装、启动、激活、文件打开、回放、分析、视频、录制与导出验收。

## Validation

### 当前环境可执行

- 冻结边车构建并运行 `--version`、`hello`、CSV/HDF5/EDF open/get_slice、录制和三格式导出。
- 发布配置静态检查、CMake Linux build、i18n、JSON、全量 pytest、M3/M4 黄金回归。
- 安装目录布局单元测试和 Native 运行时路径 smoke。

### Windows 发布门禁

- Visual Studio 2022 x64 Release 构建。
- `windeployqt` 后在无 Qt/Python 开发环境的干净 Windows 10/11 用户上启动。
- Inno Setup 安装、覆盖安装、卸载和自选目录。
- 激活、打开文件、分析、视频、录制、导出全流程。
- 使用组织代码签名证书签署 `bf_app.exe`、边车 exe 和安装包；`signtool verify /pa` 通过。
- 保留构建日志、证书主题/时间戳服务、安装包 SHA-256 和验收报告。

## Risks

- Qt Multimedia 发布包可能携带 FFmpeg；必须核对实际 Windows Qt 分发许可证和随包二进制，不能复用开发环境许可结论。
- MNE 冻结依赖动态导入；当前 `collect_all('mne')` 和实际 CSV/EDF 样本已通过，Windows 冻结产物仍须重复格式 smoke。
- RSA 授权实现与固定向量已通过，但正式公钥和旧版真实授权 key 不在仓库中；完成兼容验收前不得宣称授权对齐完成。
- 代码签名证书与 SmartScreen 声誉是外部条件；无证书时流水线只允许产出 unsigned 开发包，不得标为 `rc` 或 `stable`。

## Out of Scope

- BLE、自研硬件、真实设备录制和 12 小时长稳。
- macOS/Linux 安装包。
- 自动更新、云分发和商店发布。
