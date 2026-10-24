---
title: EEG 界面阶段 6A 空白状态与数值控件
doc_type: report
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# EEG 界面阶段 6A 空白状态与数值控件

未打开文件时隐藏整个右侧数据工作区，欢迎页居中；首次打开失败保持空白布局。没有文件时禁用数据面板的视图入口，加载成功后启用，避免出现孤立参数面板。

数值框统一留出文字和按钮空间，显式定义上下按钮与明暗箭头，按钮宽 22 逻辑像素。箭头使用内置 XPM 解码，不增加 SVG 插件打包依赖。保留直接输入及键盘步进。

验证：WSL2 Qt6 configure/build 通过；`QT_QPA_PLATFORM=offscreen BF_LAYOUT_SCREENSHOT_DIR=/tmp/bf-stage6a python3 -m pytest tests/native/test_empty_controls.py tests/native/test_fixed_layout.py -q`，8 passed（9.55s）。覆盖空态、首次打开失败、成功展开、两个主题、Fusion/Windows Qt style、1/1.5/2 缩放下箭头像素和实际鼠标点击增减，另有四语言双主题固定布局回归。Qt Windows style 在 Linux 离屏运行不等于真实 Windows 桌面与 DPI 验收。

右侧标签布局与拖动波形预览属于后续独立阶段。本提交不包含工作区已有授权 smoke 与 Windows 打包脚本修改。
