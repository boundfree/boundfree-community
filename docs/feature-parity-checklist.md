---
title: 功能对齐验收清单
doc_type: checklist
status: active
owner: BoundFree
last_updated: 2026-10-18
reference_repo: ../boundfree-eeg-upper-computer
---

# 功能对齐验收清单

新 C++/Qt6 版 v1 stable 的验收依据。逐条对照旧 Python 版（`../boundfree-eeg-upper-computer`）行为与数值。
标注负责里程碑（M#）。数值类项须通过黄金文件比对，容差写入对应测试记录。

## 范围分层

v1 stable 必达：

- 通用 EEG/LFP/电生理文件分析。
- 文件打开、懒加载、波形浏览、DSP、频谱分析、事件标注、视频文件同步、片段导出。
- 录制与 Session 骨架，可通过文件回放源或模拟源验证写盘链路。
- 离线机器授权、Windows 10/11 安装包、简体中文和英文界面。

v1 目标但需真实样本门禁：

- Intan RHD/RHS、Open Ephys、Plexon、Blackrock。
- 只有真实样本和测试覆盖后，才能在界面、发布说明或销售材料中宣称“已支持”。
- 如果样本缺少 TTL 或事件，只能验收连续信号和基础元数据，不能宣称事件支持完成。

v1.1/v2：

- 自研 `ADS1299-4 + ESP32-C3`。
- BLE 真实设备采集。
- 真实设备录制闭环。
- 设备状态监控。
- 12 小时以上采集长稳测试。

## 文件读取（边车）— M1/M2

### v1 必达对齐格式

- [x] EDF / BDF 打开（元数据 fs/通道/时长一致）
- [x] BrainVision (.vhdr) 打开
- [x] EEGLAB (.set) 打开
- [x] FIF (.fif) 打开
- [ ] Intan RHS (.rhs) 打开 + 刺激/数字事件提取
- [ ] BBK/DAT/LSD (.dat) 打开 + 量程换算
- [x] HDF5 (.h5/.hdf5) 打开（可配 dataset/通道轴）
- [x] CSV 打开（可去时间列）
- [x] 懒加载：open 只取元数据，get_slice 按需读
- [x] 单位统一换算为 μV
- [x] 后台异步加载不阻塞 UI

### v1 目标格式与样本门禁

- [ ] Intan RHD/RHS：连续信号、基础元数据、TTL/刺激事件按样本覆盖验收
- [ ] Open Ephys：连续信号、基础元数据、TTL/事件按样本覆盖验收
- [ ] Plexon：连续信号、基础元数据、事件按样本覆盖验收
- [ ] Blackrock：连续信号、基础元数据、事件按样本覆盖验收
- [ ] 每个对外宣称支持的格式均有真实样本和验收记录

## 波形显示 — M2

- [x] 原始/处理后数据切换：波形与分析使用同一来源，保留处理参数，双极参考通道名称与数量正确；验收入口 `tests/native/test_waveform_data_modes.py`，见 [数据模式报告](reports/waveform-data-modes.md)

- [x] 多通道波形（内置纵向滚动和滚轮、最小110px行高，40通道回归）
- [x] 通道显隐
- [x] 时间窗 1–120s（默认 10s）
- [x] 幅值缩放（默认逐通道自动范围，可切换统一手动幅值；见阶段 4A 报告）
- [x] 静态浏览和回放刷新（帧率达 M0 基线）
- [x] 回放/静态双模式：M5 已交付播放/暂停/seek/倍速/prefill（见 `reports/m5-playback-annotations-acceptance.md`）
- [x] 时间轴/顶点缓冲复用（相同窗口不重建显示模型）
- [x] 大窗口或高采样率数据使用 MinMax、LTTB 或等价抽稀策略
- [x] 真实 GPU/显示环境复测记录已写入 `docs/reports/m2-rendering-gpu-gate.md`

## 信号处理 — M3

- [x] 重参考 raw / CAR / bipolar（bipolar 减一通道、重建通道名）
- [x] 工频陷波 3 路独立基频（默认 50/100/150）
- [x] 陷波谐波（基频×1..N，<nyquist）
- [x] 实时滤波带状态 biquad（跨块）
- [x] 离线零相位滤波
- [x] 带通处理链（实时+离线，Butterworth 4 阶）；UI 提供全局带通开关，并支持每通道带通覆盖（`ProcessingViewState::perChannelBandpass`，缺省回落全局，`processing_view_state_test` 覆盖回落逻辑）；逐行编辑提供继承/关闭/启用与截止频率校验，非法草稿不进入处理状态
- [x] 【黄金比对】所有 DSP 路径数值一致

## 频谱分析 — M4

阶段 5A 补充：四页标题/属性/图例外置，补数值刻度、图外状态、失败/空结果清理和语言刷新；48 项针对性回归通过，见 [阶段 5A 报告](reports/eeg-ui-stage5a-analysis.md)。

当前详情展示：属性无内部滚动，按完整文字所需高度展开；PSD/相干以正常字号三列频段表格显示范围与数值，单位集中到表头。多语言下拉和窄窗口按内容调整，必要时提高窗口最小高度，见 [可读性报告](reports/eeg-ui-readable-details.md)。

- [x] PSD（Welch + Hann + dB 切换 + 频段色块）
- [x] FFT 幅度谱模式
- [x] STFT 时频图（jet 内联色阶 + 双线性平滑 + colorbar + 频段文字注释）
- [x] 频段功率趋势（后台计算 + 色块图例 + 旧请求结果抑制）
- [x] 相干性（MSC + 通道 A/B + 频段着色 + 0.5 阈值 + 均值摘要 + 同通道提示）
- [x] 后台计算、只算当前 tab、latest-request-wins
- [x] 【黄金比对】四视图数值一致（见 `docs/reports/m4-spectral-acceptance.md`）

## 事件标注 — M5

- [x] 波形点击添加标注 + 输入框
- [x] 顶通道显标签、颜色轮换（单次 signal 连接）
- [x] 标注列表编辑、删除与清空
- [x] 导入：MNE annotations / CSV `_markers.csv` 侧车；Intan RHS 读取器仍未实现，事件导入随其格式实现验收
- [x] 标注 CSV 导出与回读往返一致

## 回放控制 — M5

- [x] 播放 / 暂停 / 进度拖动 seek
- [x] 倍速 0.25/0.5/1/2/4×
- [x] prefill 窗口预取
- [x] seek 只保留最新窗口（复用 `FileOpenController` 请求代次）
- [x] 播放到尾归零并请求起始窗口
- [x] 位置时钟同步

验收记录：[`reports/m5-playback-annotations-acceptance.md`](reports/m5-playback-annotations-acceptance.md)。

2026-10-16 补充：读片会话复用、连续前向视窗及有界预取已通过自动化验证，见 [`阶段2A`](reports/eeg-ui-stage2a-session.md) 和 [`阶段2B`](reports/eeg-ui-stage2b-playback.md)。Windows桌面流畅度及旧版尾随显示语义未在此轮验收。

## 视频同步 — M6

- [x] 视频加载（mp4/avi/mov/mkv/wmv/m4v）
- [x] 逐帧渲染节流 ~20fps
- [x] 视频从属 EEG 时钟 syncTo（漂移>400ms 校正）
- [x] 偏移对齐（「应用」按钮显式触发）
- [x] 空白段黑屏（不显封面）
- [x] 倍速跟随

验收记录：[`reports/m6-video-sync-acceptance.md`](reports/m6-video-sync-acceptance.md)。

## 录制 — M7

- [x] 录制与 Session 骨架可通过文件回放源验证
- [x] 双格式同时录制（EDF + HDF5）
- [x] 后台写 + Native/边车有界队列背压 + 独立丢弃计数
- [x] 录制元数据输入（animal_id/exp_type/notes/dir），统一表单、输出目录校验、取消不启动（阶段 4B）
- [x] REC 状态 / 耗时计时 / 磁盘监控（<500 MiB 警告）
- [x] 录制中事件追加 HDF5 events（单一写线程与写句柄）
- [x] HDF5 maxshape 可扩展 / float32 / 按约一秒 chunk
- [x] 不把 BLE 真实设备采集录制作为 v1 验收项

## 片段导出 — M7

- [x] 原始/处理后独立选择：Native 处理后经二进制 IPC 写出 EDF/HDF5/CSV，覆盖参考、滤波、逐通道覆盖、标注与缓存上下文；验收入口 `tests/native/test_waveform_export_modes.py`、`tests/sidecar/test_export_samples.py`，见 [数据模式报告](reports/waveform-data-modes.md)

- [x] 异步导出 EDF / HDF5 / CSV
- [x] 导出带标注（EDF Annotations / HDF5 events / CSV 侧车）
- [x] 忙碌 / 完成 / 失败反馈
- [x] 【往返比对】三格式元数据、数据和标注语义一致

## 系统级 — M1/M2/M8

- [x] 亮/暗双主题（按旧版中性浅色/深蓝灰与紫色强调），切换同步 EEG 图元和图外属性；双主题四语言布局见阶段 5B 报告
- [x] 参数分区（通道/处理/标注）：右下固定标签页，通道和事件列表独立滚动、处理仅显示选中通道编辑器，视图菜单按标签隐藏偏好维护；见 [阶段 6B](reports/eeg-ui-stage6b-parameters.md)
- [x] 空白页隐藏完整数据侧栏；数值箭头、时间窗/幅值预设及直接输入回归通过，见 [阶段 6A](reports/eeg-ui-stage6a-empty-controls.md)
- [x] 拖动波形预览：缓存立即显示、缓存外限频合并读取，松手最终定位；真实 CSV/边车集成通过，性能门禁另验，见 [阶段 6C](reports/eeg-ui-stage6c-scrub-preview.md)
- [x] 固定区域布局：顶部信号设置、左波形/视频、右分析/参数、底部播放录制；禁用面板浮动与拖移，常用操作自动换行（离屏四语言/两尺寸已验，Windows DPI 待验，见 [阶段 3 报告](reports/eeg-ui-stage3-layout.md)）
- [x] 菜单 + 快捷键（Ctrl+O / Ctrl+Shift+O / Ctrl+Q）
- [x] 状态栏常驻文件名 + 模式标签（空闲/回放/实时/录制）：`statusFileLabel_` + `statusModeLabel_`（`currentModeKey()` 覆盖 idle/playback/recording），`--smoke-m8-system-ui` 断言
- [x] i18n：zh_CN/en_US 运行时切换
- [x] i18n：pseudo 用于布局与缺 key 检查
- [x] i18n：其他 locale 文件保持 key 完整性，不作为 v1 stable 验收项
- [x] i18n：数字/日期/单位区域格式化覆盖 v1 必达语言：`I18nService` 提供 `formatNumber/formatInteger/formatUnit/formatDuration/formatDateTime`，UI 数值展示点（频段功率、标注时间、视频偏移、波形坐标轴刻度等）已接入，单位符号保持 ASCII（状态栏 `app.status.file_window` 因 pytest 逐字节断言维持 C-locale 渲染）
- [x] i18n：边车 error_code → UI 本地化
- [x] i18n：缺 key CI 拦截 + 伪本地化
- [x] 离线授权：机器码 SHA256(MachineGuid) 一致（MAC 不参与，与旧版 get_machine_id 对齐）
- [ ] 离线授权：RSA-2048 PSS 校验与旧版判定一致、密钥格式兼容（实现与固定向量已通过；待正式公钥和旧版真实 key 验收）
- [x] 数据源抽象、Session 和实时管线扩展边界保留
- [ ] PyInstaller 边车 + Inno Setup 安装包（冻结边车和配置已验证；待 Windows 实际生成、安装、覆盖安装与卸载）
- [ ] Windows 代码签名

## v1.1/v2 硬件采集 — 后续路线

- [ ] BLE 源接口接入真实设备
- [ ] 自研 `ADS1299-4 + ESP32-C3` 协议解析
- [ ] 设备 ID、固件版本、采样率、通道数、电池电量、连接状态、丢包计数、接收速率
- [ ] 真实设备连续采集、显示和录制 12 小时以上
- [ ] 无线断开、重连和采集异常状态记录
- [ ] 采集数据进入 v1 浏览、分析、标注、视频同步和导出流程

## 已解决 bug 复现防护（来自旧版 AGENTS.md）— 全程

- [x] 标注 scene 只连一次（防死循环/多颜色）：`MarkerStore` 颜色确定性由 `--smoke-m5-markers` 钉住；UI 层 connect 次数尚无专用 smoke（partial，见 eeg-closeout 报告）
- [x] 切文件中止旧趋势计算：`--smoke-m4-analysis` 断言 latest-request-wins 与 cancel 语义
- [x] 暗模式遍历更新已建图元配色：`--smoke-m8-system-ui` 断言切暗主题后 window/highlight 色键更新
- [x] STFT jet 色阶内联定义：`--smoke-m4-render` 断言 spectrogram 非空像素（内联 jet，无外部 colormap 文件）
- [x] 视频从属时钟、无 singleShot 补偿：`--smoke-m6-sync` 断言 blank_idempotent 与从属 seek/pause/play 命令路径
- [x] 空白段黑屏：`--smoke-m6-sync` 断言空白段 blank+pause+seek0 且幂等
- [x] 边车 MNE `collect_all` 完整收录：`tests/sidecar/test_bug_regressions_spec.py` 静态断言 spec 含 `collect_all('mne')` 与 onedir COLLECT
- [x] 切换文件断开旧会话信号：`--smoke-ui-open-file-dispose` + `--smoke-ui-window-latest` 覆盖控制器析构与二次请求；跨不同 path 切换 smoke 尚无（partial）
