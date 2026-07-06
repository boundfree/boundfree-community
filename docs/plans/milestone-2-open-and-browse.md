# Milestone 2：打开文件与波形静态浏览 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development / executing-plans。
> **滚动式说明：** 任务级；细步在 M1 完成后定稿。

**Goal:** 能通过边车打开全部支持格式，并用 GPU 波形做静态浏览：多通道显示、通道显隐、时间窗调节、幅值缩放、亮/暗主题。

**Architecture:** `FileOpenController` 经 IPC 异步 `open`，拿元数据后建立会话；`WaveformView`（M0 渲染原型演进）渲染 `get_slice` 返回的静态切片；右侧通道列表与主题体系接入。

**Tech Stack:** C++/Qt6、OpenGL 渲染、IPC。

## Dependencies
- M1（IPC、边车、主窗口骨架）。

## File Structure

| 路径 | 职责 |
| --- | --- |
| `native/controller/file_open_controller.{h,cpp}` | 异步 open、错误码→提示、会话初始化 |
| `native/controller/session_controller.{h,cpp}` | 当前会话：reader 句柄、fs、ch_names、静态切片请求 |
| `native/render/waveform_view.{h,cpp}` | GPU 折线渲染、可见窗口、抽稀、通道偏移/缩放 |
| `native/render/waveform_gl.{h,cpp}` | OpenGL 底层（从 spike 演进） |
| `native/ui/channel_list_panel.{h,cpp}` | 通道显隐复选列表 |
| `native/ui/theme.{h,cpp}` | 亮/暗双主题（清华紫），坐标轴/曲线配色 |
| `native/ui/collapsible_panel.{h,cpp}` | 可折叠面板 |

## Tasks

### Task 1：会话与异步打开
- **Interfaces Produces:** `SessionController::setReader(handle, Meta)`、`requestStatic(t0, winSec)`、信号 `sliceReady(SlicePayload)`/`sliceFailed(errorCode)`。
- **Tests（TDD）:** fake IPC 下断言 open 成功建会话、失败走 error_code 提示；切换文件断开旧会话。
- **DoD:** 打开参考仓库各样本文件，元数据（fs/通道/时长）与旧版一致。

### Task 2：波形静态渲染 + 可见窗口 + 抽稀
- **Interfaces Produces:** `WaveformView::setStaticData(nCh, nSamp, uv, tStart)`、`setWindowSec(s)`、`setChannelVisible(idx, on)`、`setGain(uvPerPx)`。
- **Tests:** 抽稀在点数≫像素时启用（LTTB/minmax），窗口不变时复用顶点缓冲（等价旧版时间轴缓存）。
- **DoD:** 256ch 静态浏览达到 M0 记录的帧率；通道显隐即时生效。

### Task 3：时间窗、幅值、滚动
- **Deliverable:** 时间窗 1–120s（默认 10s），幅值缩放，垂直滚动查看被挤出的通道。
- **Tests:** 窗口/幅值改变触发重取切片或本地重算；边界（t<0、t>duration）夹取。
- **DoD:** 交互行为与旧版一致。

### Task 4：通道列表面板
- **Deliverable:** 右侧可折叠面板 + 通道复选列表，勾选联动波形显隐。
- **Tests:** offscreen 冒烟：勾选状态与曲线可见性同步。

### Task 5：亮/暗主题
- **Deliverable:** 主题切换，波形/坐标轴/面板配色同步（沿用旧版清华紫品牌）。
- **Tests:** 切换后所有已建图元配色更新（旧版此处踩过坑：需遍历更新，不能只改全局配置）。
- **DoD:** 亮暗切换无残留旧色。

## Acceptance Criteria
- 全部格式可打开并静态浏览。
- 通道显隐、时间窗、幅值、主题与旧版行为一致。
- 渲染帧率达 M0 基线。

## Parity Gate
- 勾验清单：各格式打开、多通道显示、通道显隐、时间窗、幅值缩放、亮暗主题。
