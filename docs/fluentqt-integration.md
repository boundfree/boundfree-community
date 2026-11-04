---
title: Fluent-Qt 组件接入
doc_type: architecture
status: active
owner: BoundFree
last_updated: 2026-11-02
---

# Fluent-Qt 组件接入

开发分支 `feature/fluentqt-integration` 继承设备、统一信号工作台及 ICA 候选工作，Qt 基线继续由 `config/qt-version.txt` 管理。组件接入保留现有三栏与参数，以及波形、频谱、时频、设备接收、原始录制和 ICA 的数据链路。ICA 流程操作位置按下述固定操作区统一。

`native/cmake/FluentQt.cmake` 通过 FetchContent 固定 Fluent-Qt 提交 `766e13c83599e8ca58f83e7c4917c66ae73aa559`，链接 `FluentQt::FluentQt` 静态核心，Qt 仍动态链接。首次配置需要下载该提交；离线或本机依赖可通过 `FETCHCONTENT_SOURCE_DIR_BF_FLUENTQT` 指定该提交的源码目录。升级时需重新核对 API、许可资源并回归双平台，不跟随 main 自动更新。

首批替换设备连接、工作台与 ICA 的按钮、下拉框，以及设备连接的文本输入框。`ui/fluent_controls.h` 保留 QPushButton/QComboBox/QLineEdit 继承接口，原有对象名、信号槽、翻译键、键盘焦点和参数绑定继续使用。原有自绘科学控件独立；不引入 Gallery、QML、3D、粒子、Python UI 绑定或其他原型库。Fluent 明暗主题与主窗口主题同步；保留平台字体与紧凑操作密度。

第二批增加 Fluent CheckBox、Slider、FluentMenuBar/FluentMenu 与 Expander，并把预处理、播放和导出表单中遗漏的基础控件接入同一封装。菜单栏、状态栏、表格表头、原生数值框和标签页共用 Fluent 的表面、文本、边框、强调色 token；保留 alpha，避免半透明边框被转换成纯黑。数值框保留 QSpinBox/QDoubleSpinBox 的单位后缀、locale、范围、步进与小数精度，标签页保留 QTabWidget/QTabBar 的内容和切换接口，不将它们声称为 NumberBox/TabView。

CollapsiblePanel 内部承载 Fluent Expander，保留标题、程序展开/收起和用户 toggled 接口，头部有可访问名称；程序更新与布局调整原子执行。已有标签提供标题时，内容可从 Expander 取出并直接嵌入原位置，再恢复头部，避免重复标题或多一层空白。所有权转移与重复展开均有回归覆盖。原有窗口布局、科学图形和算法继续复用。

分离页将标题、四步流程与下一步提示、当前输入摘要分区显示，活动步骤单独突出；输入摘要与分析区间/通道设置保持间距，内容随语言换行、窄窗滚动。波形浏览与分离共用 `ui/workspace_splitter.h`：12像素拖动范围、细分界线和居中三点手柄，横竖布局均支持鼠标拖动与聚焦后的对应方向键，提示与可访问名称随语言更新。

ICA 建模、重新分离、导出、连续显示、取消与重试统一位于步骤条下方的 `separationActionBar`，使用可换行的公共 `ControlBar`，不随阶段移动到右下角。主操作按任务状态切换 Fluent Accent，其余为 Standard；重试时用重试入口替换重复的主按钮。错误、条件警告、普通进度/主动取消、导出成功分别使用 `systemCritical`、`systemCaution`、普通信息和 `systemSuccess` 的明暗语义色及背景，不用硬编码红色。错误和警告有文字前缀，消息使用纯文本、可访问描述，错误可复制；恢复时同步清除旧 tooltip。`eeg_separation_workspace_test` 检查真实绘制的错误颜色、消息恢复、模型前置条件及操作位置，连续设备链路另由 `--smoke-ui-live-ica` 验证。

通道用途在当前选项与弹出列表中提供 tooltip 和可访问说明；页面右上角使用说明是统一的说明入口，当前输入旁的采集信息仍可直接查看元数据；非模态窗口提供成分检查页，已有结果时直接打开该页。左侧用途说明、中间快捷操作及分离页底部操作说明按钮已移除，原上下文tooltip保留。成分区采用编号/分类列表和四个分类单选项，上一个/下一个切换成分，波形/频谱/参考指标使用标签布局；波形标题包含IC编号，分组标题包含观测通道和贡献IC。分组和IC波形在聚焦后提供时间/幅值缩放与离线平移，时间轴同步、IC幅值独立，坐标包含刻度和实际单位；PSD采用非负纵轴，选择框同步显示实际缩放值，自定义值复用单个条目。事件仅由波形视图处理，输入控件保留自己的键盘操作，分离模型与原始录制独立。底部采集操作栏提升到工作区切换容器之外，两页共用同一实例和设备控制器，录制进度/路径移至现有状态栏，单行省略且保留完整tooltip和可访问文本；查看操作可委派给当前工作区。连续 ICA 暂停查看时保留一个显示帧与最新后台帧，原始采集/录制与投影继续；恢复跳至最新帧，暂停期间的人工标签按固定帧重新分组。

核心及字体/图标许可随 CMake install 写入 `legal/fluentqt/`；版权归属不因 Community 的 Apache-2.0 改变。上游源码 MIT，嵌入字体按 SIL OFL 1.1、图标按其 MIT/NOTICE 分发，见固定源码的 THIRD_PARTY_NOTICES.md。

验证入口是现有 `--smoke-ui-device-waveform`、`--smoke-ui-unified-tools`、`--smoke-ui-workspace-navigation` 和 `eeg_separation_workspace_test`，覆盖四语/主题/窄窗、设备与文件共享工具、录制独立、ICA 取样/重新拟合/标签/导出和重复操作。`--smoke-ui-fixed-layout` 按四种语言检查 760、900、999、1000、1280 像素宽度，分别验证窄屏的波形/分析标签切换与宽屏三栏、可滚动分析内容、面板显隐和分隔条调整；设置 `BF_LAYOUT_DARK=1` 检查深色主题，测试使用独立临时设置。实际截图、运行日志与未验收项保存在跨仓库工作记录；编译通过不能代替真实硬件或干净机验收。
