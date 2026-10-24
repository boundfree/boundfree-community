---
title: EEG 阶段 2B 连续视窗与有界预取验收
doc_type: report
status: superseded
original_status: completed
owner: BoundFree
last_updated: 2026-10-16
archived_at: 2026-10-23
---

# EEG 阶段 2B 连续视窗与有界预取验收

## 交付范围

波形数据缓存与显示时间范围分离：播放时每 25ms 推进视窗，缓存替换不改变当前视窗，平移不重建显示模型。按绝对采样时间 t0+i/fs 绘制并裁剪，仅遍历可见样本。坐标、光标、事件与点击标注共用显示时间范围。

主窗口保留一个覆盖式缓存与一个在途预取；显示窗 1–120 秒，额外前瞻 2–8 秒，内部读片最多 128 秒。缓存不足时冻结时间和从属视频，补齐后续播，不把等待时间补算成跳跃。错误或成功但不足覆盖的响应停止播放，不重试风暴。暂停后返回数据不会自动播放；打开新文件期间阻止旧文件请求覆盖新任务。

预取完成只处理新缓存并更新绘图，不重建通道/分析控件。分析输入裁剪为可见范围，播放自动分析间隔至少 500ms，暂停与参数操作即时请求。

## 显示语义

本阶段连续显示保留新版前向窗口：位置表示窗口起点，尾部钳制窗口但保留真实光标位置。暂停保留当前显示，暂停后定位恢复静态切片范围。这尚不是旧版播放光标位于右端的历史尾随模式；旧版完整布局/视觉对齐继续按阶段 3–5 进行。

## 验证结果

环境：WSL2 Linux、仓库 Qt6 conda 工具链、Qt offscreen。CMake configure、完整构建与最终 bf_app 增量构建通过。

- waveform_viewport_test：脉冲实际像素平移、离屏裁剪、鼠标点击对应时间、视窗更新不增加模型构建次数、静态恢复、换缓存保留视窗。
- --smoke-ui-continuous-playback：受控慢响应冻结、缓冲期间暂停、恢复连续前移、非逐帧请求/复制、4x、尾部与归零、新文件加载隔离、不足响应停止重试。
- --smoke-ui-continuous-file：真实临时 CSV（100秒、100Hz、2通道）经真实边车与 FileOpenController，在首片完成后以4x播放2秒。验证至少2次预取、至少3次结果、至少10次可见推进、推进超过3秒；请求数低于20ms采样次数的四分之一；缓存返回前后视窗相同，暂停200ms稳定。阈值用于集成行为验证，不作为性能基准。
- 新会话、拖动、既有浏览、频谱数值、图像、回放和视频回归均通过。

定向 pytest 文件：test_continuous_file、test_continuous_playback、test_seek_preview_smoke、test_waveform_viewport、test_file_session_reuse、test_bf_app、test_m4_analysis_smoke、test_m4_spectral_binary、test_m5_playback_smoke、test_m6_video_sync、test_spectrum_render（均位于 tests/native）。合计58项通过：联合运行57项通过、真实CSV测试初始化时按空动作文字查找控件失败，改用已翻译tooltip查找后单独复跑1项通过。产品代码在该修正中未改变。

旧预览smoke更新为有效的15秒缓存，明确区分播放初始预取和拖动请求；旧合成fixture补齐duration，维持真实元数据契约。

## 剩余边界

- 未执行 Windows 桌面/高DPI/高通道高采样率帧率验收，未打包发布。
- 新缓存仍在 UI 线程按现有离线零相位方法处理，缓存交界滤波边缘与大数据处理耗时需后续实测；未宣称已迁移到有状态实时滤波。
- 缓存有时间上限，尚无字节预算；绘图虽然只遍历可见样本，但高采样率下的逐像素包络优化仍属后续性能任务。
- 边车同步操作不能即时取消，关闭等待边界见阶段2A报告。
- 工具栏、Dock固定分区、逐通道缩放、属性移出图内尚未实施。
