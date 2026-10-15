---
title: M1-A 基础契约与可启动骨架
doc_type: plan
status: completed
owner: BoundFree
last_updated: 2026-07-21
milestone: M1-A
---

# M1-A 基础契约与可启动骨架

本文记录已完成的 M1-A 基础骨架实施计划，保留用于追溯；当前阶段实施入口见 [`plans/m4-spectral.md`](plans/m4-spectral.md)。M1-A 只建立基础契约和可启动骨架，为后续文件打开、波形浏览、DSP、授权、录制和打包提供稳定边界。

## Goal

完成一个可构建、可启动、可显示版本、可切换语言、可启动 Python 边车并完成握手的最小上位机骨架。

## Scope

M1-A 包含：

- CMake/Qt6 最小主程序。
- 从 `version.json` 读取产品版本和 IPC 协议版本。
- `bf_app --version`。
- Qt6 空主窗口、状态栏、语言菜单和边车连接状态。
- i18n 基础设施：`zh_CN`、`en_US`、`pseudo` 可实际切换，其他目标语言文件保持 key 完整。
- 统一错误码和 IPC envelope。
- Python 边车正式包：`--version`、`--stdio`、`hello`。
- C++ `SidecarClient`：启动、握手、超时、关闭。

M1-A 不包含：

- 真实 EEG 文件打开。
- 波形渲染。
- DSP 和黄金文件比对。
- 完整授权校验。
- 录制、导出和打包。

## Implementation Order

1. 创建或确认开发分支 `feature/m1-foundation`。
2. 建立 CMake/Qt6 最小程序。
3. 实现 `version.json` 读取与 `bf_app --version`。
4. 建立 i18n 服务、翻译资源、伪本地化和静态检查。
5. 建立错误码注册表与 IPC success/error envelope。
6. 建立 Python 边车正式包和 `hello` 握手。
7. 建立 C++ `SidecarClient` 生命周期管理。
8. 接入空主窗口：版本、语言切换、边车连接状态和本地化错误展示。

## Expected Files

| 路径 | 职责 |
| --- | --- |
| `native/CMakeLists.txt` | 顶层 C++/Qt6 构建入口 |
| `native/app/` | `bf_app` 入口、版本读取、应用启动 |
| `native/core/` | 版本结构、错误结构和基础工具 |
| `native/i18n/` | 翻译加载、语言切换、错误文案映射 |
| `native/ipc/` | IPC envelope、边车进程生命周期和握手 |
| `native/ui/` | 空主窗口、语言菜单、状态栏 |
| `sidecar/boundfree_sidecar/` | Python 边车正式包 |
| `tests/native/` | C++ 单元和 Qt offscreen 冒烟测试 |
| `tests/sidecar/` | Python 边车协议测试 |
| `tools/` 或 `scripts/` | i18n 和错误码静态检查脚本 |

## Acceptance Criteria

- `bf_app --version` 输出与 `version.json` 一致的产品版本。
- 主窗口可启动，不依赖真实 EEG 文件。
- 语言菜单可在 `zh_CN`、`en_US`、`pseudo` 间切换，并刷新窗口标题、菜单和状态栏。
- i18n 检查能发现缺 key、占位符不一致和 UI 硬编码中文。
- Python 边车 `--version` 输出产品版本和 IPC 协议版本。
- C++ 主程序可启动边车，发送 `hello`，验证 `ipc_protocol_version`，关闭时停止边车。
- 协议错误、边车超时和版本不匹配能通过错误码映射为本地化 UI 文案。

## Verification

文档和配置：

```bash
git diff --check HEAD
python3 -m json.tool version.json
```

C++：

```bash
cmake -B native/build -S native
cmake --build native/build
native/build/bf_app --version
```

Python：

```bash
python -m pytest tests/sidecar
python -m boundfree_sidecar --version
```

UI 与 i18n：

```bash
QT_QPA_PLATFORM=offscreen native/build/bf_app --smoke-ui
python scripts/common/check_i18n.py
```

以上命令是阶段目标。文档收口提交只要求运行文档和 JSON 校验。

## Handoff To Next Phase

M1-A 完成后进入 M1-B 或 M2 前置工作：

- 补齐授权校验与旧版密钥语义对齐。
- 扩展边车 `open/get_slice/close` 正式协议。
- 在真实 GPU/显示环境复测渲染 spike。
- 根据复测结果决定 M2 波形渲染实现细节。
