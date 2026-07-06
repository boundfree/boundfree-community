# Milestone 8：打包与验收 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans。
> **滚动式说明：** 任务级；细步在 M1–M7 完成后定稿。

**Goal:** 把 C++/Qt6 主程序 + Python 边车 + Qt 运行时打包为 Windows 安装包，完成全量功能对齐验收与代码签名，产出可发布版本。

**Architecture:** CMake 构建主程序（Release）；PyInstaller 打包边车为子目录；Inno Setup 封装为安装包（沿用旧版经验）；代码签名。

**Tech Stack:** CMake、windeployqt、PyInstaller、Inno Setup、Windows 代码签名。

## Dependencies
- M1–M7 全部完成。

## File Structure

| 路径 | 职责 |
| --- | --- |
| `native/CMakeLists.txt` | Release 构建 + windeployqt 收集 Qt 运行时 |
| `sidecar/build_sidecar.py` 或 `.spec` | PyInstaller 打包边车（`collect_all('mne')`） |
| `installer/installer.iss` | Inno Setup 脚本（`DisableDirPage=no`、法务文件） |
| `build_release.ps1` | 一键：构建主程序→打包边车→windeployqt→Inno Setup |
| `docs/feature-parity-checklist.md` | 全量勾验 |

## Tasks

### Task 1：主程序 Release 构建 + Qt 运行时收集
- **Deliverable:** Release 构建产出 exe；windeployqt 收齐 Qt DLL/插件（含 OpenGL/Multimedia 平台插件）。
- **Tests:** 在无 Qt/无 Python 开发环境的干净 Windows 上运行 exe 不缺 DLL。

### Task 2：边车 PyInstaller 打包
- **Deliverable:** 边车打为子目录，`collect_all('mne')` 完整收录（旧版踩坑：只加 hiddenimports 会运行时报 stub 错）；matplotlib 不能进 excludes。
- **Tests:** 打包后的边车对各样本 open 成功。
- **DoD:** 主程序调用打包边车全链路可用。

### Task 3：Inno Setup 安装包
- **Deliverable:** 封装主程序 + 边车 + Qt 运行时 + `LICENSE.txt`/`legal/` 法务文件；`DisableDirPage=no`。
- **Tests:** 全新机器安装→启动→激活→打开文件→录制→导出全流程通过。

### Task 4：代码签名
- **Deliverable:** 对 exe 与安装包做 Windows 代码签名（医疗器械分发需要）。
- **DoD:** 签名有效、SmartScreen 友好。

### Task 5：全量功能对齐验收
- **Deliverable:** 按 [`../feature-parity-checklist.md`](../feature-parity-checklist.md) 逐条勾验；黄金文件比对全绿。
- **DoD:** 清单 100% 通过；遗留项登记并评估。

## Acceptance Criteria
- 干净 Windows 上一键构建出安装包。
- 安装后全功能可用且与旧版对齐。
- 代码签名有效。
- 功能对齐清单全通过。

## Parity Gate
- 全量清单勾验（本里程碑即最终闸门）。
