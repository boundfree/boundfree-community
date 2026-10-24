---
title: 品牌 Logo 集成计划
doc_type: plan
status: superseded
original_status: implemented
owner: BoundFree
last_updated: 2026-07-25
archived_at: 2026-10-23
---

# 品牌 Logo 集成计划

## Objective

将用户提供的 `BoundFree_LOGO_WithText.png` 与 `BoundFree_LOGO_NoText.png` 作为正式品牌源图纳入 Native 产品，使 EEG 与 Spike 两个 Qt 应用的空白欢迎页、窗口/任务栏、Windows 可执行文件和安装包均使用同一品牌标识，并确保 CMake 构建和 M8 安装 stage 可携带所需资源。

## Current State

- Native 仓库没有 `.qrc`、PNG、ICO 或现有 Logo 资源。
- `MainWindow` 的空白页只显示状态文案和“打开”按钮，是 EEG 应用内唯一合理的常驻品牌展示位置。
- `SpikeWindow` 当前没有欢迎页；它是独立产品线原型，不应为 Logo 另建一套展示页。应继承全局 Qt 应用图标，并使窗口图标一致。
- `bf_app` 与 `bf_spike` 均无 CMake `WIN32` 图标资源；Inno Setup 也没有 `SetupIconFile`。
- 两份源图均为 1024×1024 RGBA PNG，透明像素占比超过九成；带文字版用于欢迎页，无文字版用于应用图标。

## Impacted Files

- 新增 `native/assets/branding/BoundFree_LOGO_WithText.png`：带文字 Logo 的受控副本，用于欢迎页。
- 新增 `native/assets/branding/BoundFree_LOGO_NoText.png`：无文字 Logo 的受控副本。
- 新增 `native/assets/branding/BoundFree_app_icon.png` 与 `.ico`：从无文字 Logo 的有效透明边界生成的应用图标。
- 新增 Qt 资源清单（置于 `native/resources/`），将 PNG 编译入两个 Native 可执行文件。
- 修改 `native/CMakeLists.txt`：将 Qt 资源链接到 `bf_app` 和 `bf_spike`；Windows 下为两个可执行文件写入同一 `.ico`。
- 修改 `native/src/app/main.cpp` 与 `native/src/app/spike_main.cpp`：在创建窗口前设置 `QApplication` 图标。
- 修改 `native/src/ui/main_window.{h,cpp}`：在 EEG 空白欢迎页显示受 Qt 资源管理的原始 Logo，按保留纵横比缩放，不依赖工作目录或安装目录。
- 修改 `installer/BoundFreeEEG.iss`：用同一 `.ico` 设置安装器图标；保留卸载显示图标继续指向已带图标的 `bf_app.exe`。
- 修改 `scripts/common/check_release_assets.py` 和对应发布资产测试：验证 PNG/ICO 存在、CMake/安装器引用一致，防止打包时静默遗漏。

## Changes

1. 复制带文字和无文字 PNG 到 Native 的品牌资源目录；从无文字 PNG 的有效透明内容生成包含常用 Windows 尺寸的 ICO。
2. 用 Qt Resource System 嵌入 PNG。EEG 欢迎页在状态文案上方放置居中、不可拉伸变形的 Logo；文件打开后切换至波形页，Logo 不占用分析工作区。
3. 在 `QApplication` 设置全局窗口图标，使 EEG 和 Spike 的标题栏、任务栏/窗口切换器走同一资源；在 Windows 目标属性和 Inno Setup 中使用同一 ICO，使 exe、快捷方式、安装器和卸载项一致。
4. 扩展静态发布资产门禁，检查品牌文件、资源清单、目标链接和 `SetupIconFile`。新增或扩展行为测试，仅断言嵌入资源可加载、全局应用图标非空、欢迎页 Logo 有效，不依赖像素颜色或宿主桌面环境。

## Risks

- Windows 资源编译器对 ICO 内容敏感；ICO 必须包含标准尺寸/色深，且从原始 PNG 无损生成。
- 两份源图已验证为透明 RGBA；本次不修改公司图形。若实际视觉验收显示辨识度不足，应由品牌方提供深浅两套源图后再替换资源。
- Linux offscreen 测试不能证明 Windows shell 缓存已刷新；需在 Windows Release/安装 stage 手工确认 exe、安装器及快捷方式图标。

## Validation

1. 运行 CMake configure/build，覆盖 `bf_app`、`bf_spike` 与资源编译。
2. 运行受影响的 Native UI smoke/品牌资源测试，确认 `:/` 资源有效、应用图标非空且 EEG 欢迎页存在 Logo。
3. 运行 `python3 scripts/common/check_release_assets.py` 和发布资产测试，确认安装器引用及文件布局有效。
4. 在 Windows x64 Release stage 启动 EEG 与 Spike，人工确认窗口/任务栏图标、欢迎页 Logo、`bf_app.exe` 图标及 Inno Setup 安装器图标；该项作为 M8 Windows 发布门禁证据的一部分。
