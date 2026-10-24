---
title: EEG 界面阶段 6B 参数布局与常用数值输入
doc_type: report
status: superseded
original_status: active
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# EEG 界面阶段 6B 参数布局与常用数值输入

右下参数区域改为通道、处理、事件三个固定标签页，移除重复折叠标题，取消原外层滚动与通道/事件列表之间的嵌套滚动。视图菜单按标签是否隐藏保持勾选状态，不随切换页面误改变；同文件定位保留隐藏选择，新文件恢复默认页面。

逐通道滤波通过通道下拉一次编辑一个通道，列表显示各通道继承、启用或禁用状态。继承、覆盖、非法草稿校正、批量应用和清除覆盖的处理逻辑保持原约定。编辑框至少 120 逻辑像素，窄页允许整组换行。隐藏行保留以保存草稿及控件身份，本阶段没有减少底层控件数量，也不宣称大通道数内存优化。

时间窗和幅值改为可直接输入的常用值下拉控件，取消这两个高频字段的上下小箭头。窗口预设 1/2/5/10/20/30/60/120 秒，幅值预设 50/100/200/500/1000/2000/5000 µV；菜单依当前 locale 与单位生成，保留手输和键盘操作。自动幅值开启时手动幅值仍禁用。

验证：configure/full build 通过。`QT_QPA_PLATFORM=offscreen BF_LAYOUT_SCREENSHOT_DIR=/tmp/bf-stage6b-final python3 -m pytest tests/native/test_empty_controls.py tests/native/test_fixed_layout.py tests/native/test_channel_parameters.py tests/native/test_bf_app.py tests/native/test_analysis_presentation.py tests/native/test_waveform_channels.py tests/native/test_waveform_viewport.py tests/native/test_record_form.py -q` 首轮 55 passed / 1 failed（18.12s）；失败为 M8 旧测试要求无文件也可切换数据面板，与新的空白状态冲突。已更新为先验证空态禁用，再加载数据验证菜单隐藏标签。定向重跑该项 1 passed（0.35s），累计 56 个不同用例通过，并非最终一次全量 56 项运行。

覆盖四语言、双主题、900×560/1280×800，单页可达/不重叠、View 偏好、同会话定位、64 通道编辑高度不增长、草稿及设置保留、箭头与预设选择/直接输入。首次加载失败及已有会话重开失败均检查显示一致性；不存在的 Qt style 返回跳过而不冒充验证。截图确认后修复了全局频率输入原最大宽度 90 引起的文字裁切，最终回归已包含此修复。

真实 Windows 显示与高 DPI 门禁仍待执行，Linux 离屏 Qt Windows style 不等同 Windows 系统验收。小窗受分析图最小高度约束，处理页仍允许独立滚动；通道数增加不再增长可见编辑区高度。
