---
title: EEG 控件尺寸与多语言下拉列表修复
doc_type: report
status: active
owner: BoundFree
last_updated: 2026-10-16
---

# EEG 控件尺寸与多语言下拉列表修复

工具栏与分析页原先混用了原生高度的下拉框、带最小高度的数值框和不同内边距的按钮，导致同一行高低不齐。输入框、下拉框、数值框及业务操作按钮现在采用按正常字体高度计算的统一高度，保留原字号和按内容计算的宽度。流式布局逐行垂直居中，窄窗口仍整组换行。

工具栏操作按钮通过独立属性应用尺寸规则，避免影响标签页滚动箭头和预设框内部按钮。数值框内嵌编辑器单独重置内边距与高度，防止外层尺寸规则重复施加。下拉箭头继续使用内置资源。

参考模式的原生弹出菜单会为选中标记、图标和边距预留空间，仅判断文字宽度小于整行宽度不足以防止省略。弹出列表现在按当前语言最长文字加原生菜单留白设置最小宽度，切换语言重新计算，不使用会反馈上次列表宽度的 delegate 尺寸作为生产尺寸来源。回归额外比较实际 delegate 尺寸并限制列表宽度，防止文字裁切与切语言后宽度累积。

## 验证

完整 CMake 构建通过。针对参考菜单、空态控件、固定区域布局、分析详情及播放刷新运行原生回归；参考菜单覆盖 Fusion/Windows 两种 Qt 样式、100%/150%/200% 缩放、简中/英文/日文/繁中/伪本地化、浅深主题和三个参考选项。检查选中值可用文字区域、弹出列表 delegate 宽度、切语言不累积增长、工具栏及分析页同级编辑器/按钮等高。

回归命令：

```bash
QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native/test_reference_labels.py tests/native/test_empty_controls.py tests/native/test_fixed_layout.py tests/native/test_analysis_text.py tests/native/test_analysis_refresh.py -q
```

最终结果 **16 passed in 29.45s**。版本一致性、i18n、JSON 与 diff 检查通过。

截图确认英文 Bipolar、日文「バイポーラ」完整显示，并检查正常字号和控件对齐。Qt Windows 样式运行于 Linux offscreen，不代表 Windows 真机验收；Windows 实际字体、显示缩放仍需用户实测。未生成安装包、未推送，原有授权 smoke 和 Windows 打包未提交修改保持原状。
