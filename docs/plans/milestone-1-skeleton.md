---
title: Milestone 1：工程骨架实施计划
doc_type: plan
status: active
owner: BoundFree
last_updated: 2026-07-06
milestone: M1
---

# Milestone 1：工程骨架 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans。Steps 用 `- [ ]` 跟踪。
> **滚动式说明：** 本计划为任务级；每个任务的 2–5 分钟细步在 M0 通过、正式开工前依据 spike 结论定稿。

**Goal:** 搭起可长期演进的 C++/Qt6 工程骨架，打通与 Python 边车的 IPC，并让主程序能启动、激活授权、与边车握手。

**Architecture:** 正式目录结构（`native/{app,ui,controller,dsp,realtime,ipc,license,i18n}` + `sidecar/`），复用 M0 验证过的 CMake/IPC/渲染原型（转为小端零拷贝 + 异步读）。

**Tech Stack:** C++17、Qt6（Widgets/OpenGLWidgets/Core）、CMake+vcpkg、OpenSSL/Botan、Python 边车。

## Global Constraints

见 [`README.md` 全局约束](README.md#贯穿所有里程碑的全局约束)。

## Dependencies

- 依赖 M0 通过（渲染与 IPC 结论）。

## File Structure

| 路径 | 职责 |
| --- | --- |
| `version.json` | 产品版本、发布通道、IPC 协议版本的单一人工维护来源 |
| `native/CMakeLists.txt` | 顶层构建，vcpkg 工具链，子目录聚合 |
| `native/app/main.cpp` | 入口：QApplication、启动边车、授权闸门、显示主窗口 |
| `native/ipc/sidecar_client.{h,cpp}` | 边车进程管理、帧协议（小端）、异步读、超时/重启 |
| `native/ipc/protocol.h` | 请求/响应结构与方法名常量（单一真源） |
| `native/license/license.{h,cpp}` | 机器码、RSA-2048 PSS 校验（OpenSSL/Botan） |
| `native/license/machine_id.{h,cpp}` | SHA256(MachineGuid+MAC) → 32位十六进制 |
| `native/i18n/i18n.{h,cpp}` | 翻译加载、error_code→文案映射、QLocale 数字格式 |
| `native/i18n/ts/*.ts` | 8 语言翻译源（Linguist） |
| `native/ui/main_window.{h,cpp}` | 空主窗口骨架（菜单/状态栏/模式标签占位） |
| `sidecar/boundfree_sidecar/__main__.py` | 正式边车入口（从 spike 演进，含 open/get_slice + 生命周期） |
| `sidecar/boundfree_sidecar/codecs/` | 文件读写实现（初期直接复用参考仓库 eeg_io） |
| `tests/native/` | C++ 单元测试（Qt Test 或 Catch2） |
| `tests/sidecar/` | 边车协议 pytest |

## Tasks

### Task 1：顶层 CMake + 目录骨架
- **Deliverable:** `cmake --build` 通过，产出空可执行文件；vcpkg 拉取 Qt6/OpenSSL；`bf_app --version` 从根目录 `version.json` 输出产品版本。
- **Tests:** 构建成功；`bf_app --version` 打印与 `version.json` 一致的版本。
- **DoD:** CI 可在干净机器上从零构建。

### Task 2：IPC 协议正式化（小端零拷贝 + 异步）
- **Interfaces Produces:** `SidecarClient::open(path)->Meta`、`getSlice(t0,t1)->SliceResult`、`request(method,json)->json`；`protocol.h` 定义方法名常量与字节序（小端）。
- **Tests（TDD）:** 起一个 fake 边车（pytest 侧或 C++ stub），断言帧编解码、并发请求排队、边车崩溃后可重启。
- **DoD:** 往返 `open`/`get_slice` 数值正确（与参考仓库同文件同切片逐位一致，float32）。

### Task 3：边车正式化与生命周期
- **Deliverable:** `sidecar/boundfree_sidecar/` 包，`open/get_slice/close` 完整；主程序负责启动/健康检查/异常重启；边车只回 error_code。
- **Tests:** pytest 覆盖各格式 open 返回元数据；异常路径返回 error_code 而非崩溃。
- **DoD:** 边车对参考仓库 `samples/` 所有样本 open 成功。

### Task 4：机器码与授权校验（原生）
- **Interfaces Produces:** `License::machineId()->string`、`License::verify(keyText, machineId)->Result{valid, expiry, version}`。
- **Tests（TDD）:** 用参考仓库 `license/keygen.py` 生成的测试密钥，断言：合法密钥通过、篡改载荷失败、过期失败、机器码不匹配失败；机器码格式与旧版一致。
- **DoD:** 与旧版 `validator.py` 对同一密钥判定结果一致（PSS.AUTO 等价）。

### Task 5：i18n 骨架 + 完整性校验 + 伪本地化
- **Interfaces Produces:** `tr(key)`（Qt）、`I18n::formatNumber/formatDateTime`（QLocale）、`I18n::messageForError(code, params)->localized`。
- **Deliverable:** 8 个 `.ts` 建立（初期从参考仓库 catalog 迁移 key）；一个 pseudo-locale 生成脚本。
- **Tests:** 校验测试遍历所有 key，断言每语言无缺、占位符/ICU 参数一致；error_code 映射有兜底。
- **DoD:** 缺 key 时 CI 失败。

### Task 6：主窗口骨架 + 启动闸门
- **Deliverable:** `main.cpp` 串起：启动边车 → 授权校验（未授权弹激活对话框）→ 显示空主窗口（菜单/状态栏/模式标签/语言切换）。
- **Tests:** offscreen 冒烟测试：启动不崩溃、语言切换刷新、未授权走激活分支。
- **DoD:** 双击可启动到主窗口；关闭时优雅停止边车。

## Acceptance Criteria
- 干净机器可一键构建。
- 主程序启动→授权→握手边车→显示主窗口全链路跑通。
- IPC 数值与旧版逐位一致；授权判定与旧版一致；i18n 缺 key 有 CI 拦截。

## Parity Gate
- 对照 [`../feature-parity-checklist.md`](../feature-parity-checklist.md) 勾验：授权、i18n 切换、边车 open 各格式。
