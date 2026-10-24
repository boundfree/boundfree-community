---
title: EEG 界面修复阶段 3：固定区域布局
doc_type: report
status: superseded
original_status: completed
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# EEG 界面修复阶段 3：固定区域布局

## 范围与旧版依据

本阶段处理功能入口拥挤和面板自由漂移，保留阶段 1–2 的分析与回放逻辑。参考归档 `../archive/boundfree-eeg-upper-computer/boundfree_eeg/ui/main_window.py` 的顶部信号设置、左侧波形/视频、右侧分析/可折叠参数、底部录制/回放布局。旧版只读。

逐通道幅值、逐行带通和统一录制元数据表单属于阶段 4；图外属性、图例和主题细节属于阶段 5，不在本阶段宣称完成。

## 实现

- 顶部按组放时间窗、幅值、首通道、参考、陷波总开关和标注模式；标签与输入框共同换行。
- 左侧波形和视频采用垂直分隔条；右侧分析页和参数滚动区采用垂直分隔条。左右分隔条只调整比例。
- 通道、处理和标注位于右侧可折叠区域。高级陷波和带通参数从拥挤的单行工具栏移入处理区。
- 底部独立显示上一窗、下一窗、播放/暂停、倍速、录制及导出，下一行放时刻输入和伸展进度条。按钮显示操作文字，随动作状态和语言更新。
- 新增内部 `ControlBar` / `ControlFlowLayout`，使用原生控件换行，不再通过工具栏溢出菜单隐藏操作。分析参数和视频控制也随可用宽度换行。
- 四个 `QDockWidget` 仅作为固定容器嵌入布局：无标题拖动条、不可浮动、不可重新停靠；视图菜单仍可显示/隐藏。同文件定位不再重新显示用户隐藏的面板，新文件仍显示默认分析、通道和标注。
- 避免隐藏欢迎页、长通道名及视频帧尺寸撑大工作区。视频文件名独立换行显示。

保持现有数值处理、播放缓存和视频同步契约。录制仍使用现有元数据输入流程。

## 验证

验证使用 WSL2 Qt6 工具链与 `QT_QPA_PLATFORM=offscreen`。新增 `--smoke-ui-fixed-layout`，检查四种正式语言、900×560 / 1280×800、四分析页、分隔条调整、四面板开关、视频显示、常用控件点击命中、高级参数滚动可达，以及同文件定位保持隐藏面板。`BF_LAYOUT_SCREENSHOT_DIR` 可导出截图，产物不进仓库。

初次完整回归中，50 项播放、分析、视频和基础 UI 检查通过；M7 录制与导出两项失败（`record.open_failed` / `roundtrip_failed`）。未修改代码、环境或依赖，隔离复跑 `tests/native/test_m7_record_export_smoke.py` 得到 **2 passed in 20.64s**。原导出文件也可直接重新打开。首次失败原因未定位，不归因于缺依赖或本次布局，也不宣称录制链路完全无风险。

最终验证：

- `cmake --preset wsl2-qt6-conda` 与完整 `cmake --build --preset wsl2-qt6-conda -j 4` 通过。
- `QT_QPA_PLATFORM=offscreen BF_LAYOUT_SCREENSHOT_DIR=/tmp/bf-stage3-final python3 -m pytest tests/native/test_fixed_layout.py tests/native/test_bf_app.py tests/native/test_continuous_file.py tests/native/test_continuous_playback.py tests/native/test_seek_preview_smoke.py tests/native/test_m4_analysis_smoke.py tests/native/test_m5_playback_smoke.py tests/native/test_m6_video_sync.py -q`：**51 passed in 14.97s**。
- 合计 53 项针对性测试通过（上述 51 项 + 单独复跑 M7 的 2 项），保留首次 M7 失败记录。
- 四语言两尺寸截图及视频布局已生成；人工检查中文大小窗口、英文小窗口和显示视频状态。发现并修复空态页最小高度传播、换行控件的最小高度策略、分析/视频空间分配和常驻操作文字重译。
- `version_sync.py check`、`check_i18n.py`、`json.tool version.json`、`git diff --check HEAD` 通过。布局机械扫描无输出项，其对原生 Qt 的覆盖有限，不替代截图和交互检查。

版本为 `0.1.69-dev`。本阶段保持独立本地提交，未推送、未生成安装包。

## 未覆盖与后续

离屏几何与交互验证不能替代 Windows 桌面、高 DPI 和真实显示验收。图内标题/纵轴文字在语言切换后仍可能保留上次分析结果的语言，归入阶段 5 的图表文本刷新；本阶段只验证常驻控件和菜单重译。录制状态与统一元数据表单仍按阶段 4 推进。
