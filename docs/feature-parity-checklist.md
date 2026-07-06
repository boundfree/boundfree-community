---
title: 功能对齐验收清单
doc_type: checklist
status: active
owner: BoundFree
last_updated: 2026-07-06
reference_repo: ../boundfree-eeg-upper-computer
---

# 功能对齐验收清单

新 C++/Qt6 版**功能不变**的验收依据。逐条对照旧 Python 版（`../boundfree-eeg-upper-computer`）行为与数值。
标注负责里程碑（M#）。数值类项须通过黄金文件比对（容差已在对应测试记录）。

## 文件读取（边车）— M1/M2

- [ ] EDF / BDF 打开（元数据 fs/通道/时长一致）
- [ ] BrainVision (.vhdr) 打开
- [ ] EEGLAB (.set) 打开
- [ ] FIF (.fif) 打开
- [ ] Intan RHS (.rhs) 打开 + 刺激/数字事件提取
- [ ] BBK/DAT/LSD (.dat) 打开 + 量程换算
- [ ] HDF5 (.h5/.hdf5) 打开（可配 dataset/通道轴）
- [ ] CSV 打开（可去时间列）
- [ ] 懒加载：open 只取元数据，get_slice 按需读
- [ ] 单位统一换算为 μV
- [ ] 后台异步加载不阻塞 UI

## 波形显示 — M2

- [ ] 多通道波形（垂直滚动）
- [ ] 通道显隐
- [ ] 时间窗 1–120s（默认 10s）
- [ ] 幅值缩放（默认 ±200μV）
- [ ] 实时刷新（帧率达 M0 基线）
- [ ] 回放/静态双模式
- [ ] 时间轴/顶点缓冲复用（相同窗口不重建）

## 信号处理 — M3

- [ ] 重参考 raw / CAR / bipolar（bipolar 减一通道、重建通道名）
- [ ] 工频陷波 3 路独立基频（默认 50/100/150）
- [ ] 陷波谐波（基频×1..N，<nyquist）
- [ ] 实时滤波带状态 biquad（跨块）
- [ ] 离线零相位滤波
- [ ] 单通道带通（实时+离线，Butterworth 4 阶）
- [ ] 【黄金比对】所有 DSP 路径数值一致

## 频谱分析 — M4

- [ ] PSD（Welch + Hann + dB 切换 + 频段色块）
- [ ] FFT 幅度谱模式
- [ ] STFT 时频图（jet 内联色阶 + 双线性平滑 + colorbar + 频段文字注释）
- [ ] 频段功率趋势（后台计算 + 色块图例 + 切文件中止旧计算）
- [ ] 相干性（MSC + 通道 A/B + 频段着色 + 0.5 阈值 + 均值摘要 + 同通道提示）
- [ ] 后台计算、只算当前 tab、latest-request-wins
- [ ] 【黄金比对】四视图数值一致

## 事件标注 — M5

- [ ] 波形点击添加标注 + 输入框
- [ ] 顶通道显标签、颜色轮换（只连一次 scene）
- [ ] 标注列表增删
- [ ] 导入：MNE annotations / Intan 事件 / CSV `_markers.csv` 侧车
- [ ] 标注 CSV 导出与回读往返一致

## 回放控制 — M5

- [ ] 播放 / 暂停 / 进度拖动 seek
- [ ] 倍速 0.25/0.5/1/2/4×
- [ ] prefill 平滑起播
- [ ] seek 只保留最新窗口（过期切片不覆盖）
- [ ] 播放到尾归零
- [ ] 位置时钟同步

## 视频同步 — M6

- [ ] 视频加载（mp4/avi/mov/mkv/wmv/m4v）
- [ ] 逐帧渲染节流 ~20fps
- [ ] 视频从属 EEG 时钟 syncTo（漂移>400ms 校正）
- [ ] 偏移对齐（「应用」按钮显式触发）
- [ ] 空白段黑屏（不显封面）
- [ ] 倍速跟随

## 录制 — M7

- [ ] 双格式同时录制（EDF + HDF5）
- [ ] 后台写 + 有界队列背压 + 丢弃计数
- [ ] 录制元数据对话框（animal_id/exp_type/notes/dir）
- [ ] REC 闪烁 / 耗时计时 / 磁盘监控（<500MB 警告）
- [ ] 录制中事件追加 HDF5 events（单一写句柄）
- [ ] HDF5 maxshape 可扩展 / float32 / 按秒 chunk

## 片段导出 — M7

- [ ] 异步导出 EDF / HDF5 / CSV
- [ ] 导出带标注（EDF Annotations / HDF5 events / CSV 侧车）
- [ ] 忙碌 / 完成 / 失败反馈
- [ ] 【黄金比对】导出往返一致

## 系统级 — M1/M2/M8

- [ ] 亮/暗双主题（清华紫），切换同步所有图元配色
- [ ] 可折叠面板（通道/滤波/标注）
- [ ] 菜单 + 快捷键（Ctrl+O / Ctrl+Shift+O / Ctrl+Q）
- [ ] 状态栏常驻文件名 + 模式标签（空闲/回放/实时/录制）
- [ ] i18n：zh_CN/zh_TW/en_US/ja_JP/ko_KR/de_DE/fr_FR/es_ES 运行时切换
- [ ] i18n：数字/日期/单位区域格式化（德/法小数逗号）
- [ ] i18n：边车 error_code → UI 本地化
- [ ] i18n：缺 key CI 拦截 + 伪本地化
- [ ] 离线授权：机器码 SHA256(MachineGuid+MAC) 一致
- [ ] 离线授权：RSA-2048 PSS 校验与旧版判定一致、密钥格式兼容
- [ ] 数据源抽象保留（BLE 源接口占位）
- [ ] 采样率 256/512/1024、ADS1299 配置
- [ ] PyInstaller 边车 + Inno Setup 安装包（DisableDirPage=no、法务文件）
- [ ] Windows 代码签名

## 已解决 bug 复现防护（来自旧版 AGENTS.md）— 全程

- [ ] 标注 scene 只连一次（防死循环/多颜色）
- [ ] 切文件中止旧趋势计算
- [ ] 暗模式遍历更新已建图元配色
- [ ] STFT jet 色阶内联定义（不依赖外部 colormap）
- [ ] 视频从属时钟、无 singleShot 补偿（防竞态）
- [ ] 空白段黑屏（防显第 0 帧封面）
- [ ] 边车 MNE `collect_all` 完整收录（防打包 stub 错）
- [ ] 切换文件断开旧会话信号（防播放键失效）
