---
id: review-010
title: C++/Qt6 原生主程序 + Python 编解码边车 架构设计
status: approved
reviewed_at: 2026-07-04
reference_repo: ../boundfree-eeg-upper-computer  # 旧 Python 版，仅作参考与黄金文件来源
supersedes_decision:
  - 参考仓库 docs/08-architecture-baseline.md 中「继续使用 Python + PySide6，不进行语言迁移」的决策
scope:
  - native/   —— C++/Qt6 主程序（本仓库新建）
  - sidecar/  —— Python 编解码边车（本仓库新建，复用旧版文件读写实现）
---

# C++/Qt6 原生主程序 + Python 编解码边车 架构设计

> 本文是一次架构迁移的设计规格（spec）。目标是在**保持现有全部功能不变**的前提下，
> 把上位机从 Python/PySide6 迁移到 C++/Qt6 原生主程序，以获得更高的性能天花板、
> 更确定的实时延迟（摆脱 GIL）和更长的可维护寿命；同时用一个 Python 编解码边车
> 永久保留 MNE 的科研文件格式覆盖能力。

## 1. 背景与动机

现有上位机（约 7000 行 Python，PySide6 + pyqtgraph + numpy/scipy + MNE）功能完整、
已修复大量边界 bug，但存在性能天花板：

- **痛点**：多通道实时波形卡顿、内存占用增长、大文件处理慢、担心扩展到高通道数扛不住。
- **目标规模**：64–256 通道 @ 1kHz+，长期高性能与高稳定。
- **根因判断**：Python 的 GIL 使「实时采集 + 实时滤波 + 实时渲染 + 边采边存录制」
  这四条 CPU 密集路径本质上争抢同一把解释器锁，通道数越高越吃力。加速热点只能缓解、
  无法拆除这个天花板。

### 已锁定决策

| 决策 | 结论 |
| --- | --- |
| 路线 | 原生主程序 + Python 编解码边车（不做纯 Python 混合加速） |
| 主程序语言/框架 | C++ / Qt 6 |
| 边车职责 | 负责**所有文件格式的读 + 写**（复用 MNE/pyedflib/h5py/pandas） |
| IPC | 控制/元数据走 JSON over 本地 socket 或 stdio；批量 EEG 数组走二进制，必要时升级共享内存 |
| 授权校验 | 留在 C++ 原生（OpenSSL/Botan 实现 RSA-2048 PSS） |
| i18n | Qt Linguist `.ts/.qm` + QLocale/ICU 数字本地化，按 8 语言设计 |
| 明确不做 | RTL 布局镜像、微服务、语言级混用 |

## 2. 总体架构

```
┌───────────────────────────────────────────────────────────┐
│                   C++ / Qt 6 原生主程序                       │
│                                                             │
│  UI 层 (QtWidgets/QML)   渲染层 (QOpenGLWidget 折线渲染)      │
│  ─────────────────────   ──────────────────────────────    │
│  控制层 (Session/Playback/Export 协调)                       │
│  ─────────────────────────────────────────────             │
│  DSP 引擎        实时管线/环形缓冲      授权 (OpenSSL)         │
│  (Eigen+pocketfft)   (无 GIL, 真并行)   i18n (Linguist)      │
└──────────────────────────┬──────────────────────────────┘
                           │  本地 IPC
                           │  控制: JSON (socket/stdio)
                           │  批量: 长度前缀二进制 → (可选)共享内存
┌──────────────────────────┴──────────────────────────────┐
│           Python 编解码边车 (PyInstaller 打包内置)           │
│                                                           │
│  文件读取: MNE (EDF/BDF/BrainVision/EEGLAB/FIF)             │
│           自研 Intan RHS / BBK-DAT / HDF5 / CSV            │
│  文件写入: pyedflib (EDF) / h5py (HDF5) / pandas (CSV)      │
│  只返回数据与 error_code，从不返回已翻译文案                  │
└───────────────────────────────────────────────────────────┘
```

### 分层职责

| 层 | 位置 | 职责 | 不应承担 |
| --- | --- | --- | --- |
| UI | `native/ui/` | 控件布局、QML、绘图呈现、主题、i18n 呈现、弹窗、状态栏 | 文件解析、DSP 算法、IPC 细节 |
| Render | `native/render/` | GPU 折线波形、频谱/时频图渲染 | 业务决策、数据获取 |
| Controller | `native/controller/` | 会话生命周期、播放状态机、切片/分析/导出协调 | 具体绘图、文件格式解析 |
| DSP | `native/dsp/` | 重参考、陷波、带通、PSD、STFT、相干性、频段功率 | Qt widget、IPC、文件 IO |
| Realtime | `native/realtime/` | 环形缓冲、实时管线、录制缓冲、（未来）BLE 采集接入 | UI、文件格式编码 |
| IPC | `native/ipc/` | 与边车的协议编解码、连接管理、超时/重连 | 业务逻辑、UI |
| License | `native/license/` | 机器码、RSA-2048 PSS 校验、激活状态 | UI 呈现细节 |
| i18n | `native/i18n/` | 翻译加载、区域数字/日期格式化、error_code→文案映射 | 业务逻辑 |
| Sidecar | `sidecar/` | 所有文件格式读写、返回数据/error_code | 任何 UI、任何本地化文案 |

依赖方向：`ui → controller → {dsp, realtime, ipc, license}`；`ipc → sidecar(经协议)`；下层不反向依赖 UI。

## 3. IPC 协议设计

### 3.1 传输

- **控制通道**：主程序启动边车子进程；控制消息用 **JSON**，通过本地 socket（Windows 命名管道 / TCP 环回）或 stdio 传输，长度前缀分帧。
- **批量数据通道**：EEG 切片等大数组用**长度前缀二进制**（`[n_channels][n_samples][float32 payload]`），避免 JSON 编解码开销。
- **升级路径**：若 Milestone 0 的性能画像显示二进制拷贝成为瓶颈，再引入**共享内存**（Windows FileMapping / Qt `QSharedMemory`）传大数组，控制通道仍走 JSON。先简后繁，不预先过度设计。

### 3.2 请求/响应（初版消息集）

| 方法 | 请求参数 | 响应 |
| --- | --- | --- |
| `open` | `path` | `{fs, ch_names[], duration, n_channels, markers[]}` 或 `{error_code, params}` |
| `get_slice` | `t0, t1` | 二进制数组 `[n_ch, n_samp] float32 μV` |
| `close` | `handle` | `{ok}` |
| `record_open` | `fs, ch_names[], out_dir, meta, formats[]` | `{handle}` |
| `record_write` | `handle, 二进制 chunk` | `{ok, backlog, dropped}` |
| `record_event` | `handle, ts, label` | `{ok}` |
| `record_close` | `handle` | `{ok, paths[]}` |
| `export_segment` | `src, t0, t1, path, format, markers[]` | `{ok, marker_count}` 或 `{error_code}` |

### 3.3 错误码边界（i18n 关键约定）

边车**永远只返回 `error_code` + 结构化 `params`**（例如 `{"error_code":"hdf5.dataset_missing","params":{"dataset":"eeg","available":["a","b"]}}`），
主程序 i18n 层负责映射为当前语言文案。边车内不出现任何用户可见的翻译字符串。

## 4. DSP 引擎

- **库选型**：`Eigen`（线性代数/向量化）+ `pocketfft`（**BSD 许可**，FFT）。
  - ⚠️ **明确避开 FFTW / KFR**：二者为 GPL，会污染闭源专有产品；pocketfft 为 BSD，商业安全。
- **滤波**：SOS 双二阶级联（Direct Form II Transposed biquad）。
  - 实时路径：带状态 biquad（等价现有 `sosfilt` + zi 跨块），逐块推进不丢状态。
  - 离线路径：前向-后向零相位（等价 `sosfiltfilt`）。
  - 滤波器设计（Butterworth 带通、iirnotch 陷波）：按现有 scipy 行为在 C++ 复刻系数计算，
    以「黄金文件」比对验证数值一致。
- **谱分析**：Welch PSD、STFT（spectrogram）、幅度谱、`scipy.signal.coherence` 等价的 MSC、频段功率积分。
- **数据约定**：`[n_channels, n_samples]`，单位 μV，与现有约定一致。分析计算跑在工作线程，
  不阻塞 UI，无 GIL 后可真并行。

## 5. 波形渲染

- **方案**：自研 `QOpenGLWidget` 折线渲染器。理由：256 通道 @ 1kHz 的实时绘制，
  现成图表库（QtCharts/QCustomPlot）普遍吃力；自绘 GPU 折线可控性与性能最佳。
- **要点**：
  - 顶点缓冲复用 + 仅上传可见窗口内数据（沿用现有「只画可见窗口」原则）。
  - 通道偏移/幅值缩放在着色器或顶点变换里做，避免 CPU 端逐点搬运。
  - 支持通道显隐、时间窗调节、标注叠加（顶通道标签）。
  - 降采样（LTTB 或 min/max 抽稀）用于窗口内点数远超像素宽度时。
- **频谱/时频/趋势/相干性**：可用 QtCharts 或同一自绘管线的图像渲染（时频图为 2D 图像上色 + colorbar）。

## 6. 实时管线与录制

- **环形缓冲**：无锁或细粒度锁的环形缓冲承接（未来）BLE 采集与文件回放推流。
- **真并行**：采集线程、DSP 线程、渲染（主线程 GPU 提交）、录制编码（经边车）各自独立，
  摆脱 GIL 后不再互相抢锁。
- **录制**：主程序实时管线把原始 chunk 通过 IPC 批量通道交给边车，由边车用 pyedflib/h5py
  编码为 EDF + HDF5（沿用现有双格式、有界队列背压、丢弃计数、事件追加语义）。
  录制吞吐（256ch×1kHz×4B ≈ 1MB/s，按块批量）在 IPC/共享内存下无压力。

## 7. 文件格式边车

- **读**：EDF/BDF/BrainVision/EEGLAB/FIF 经 MNE；Intan RHS / BBK-DAT / HDF5 / CSV 复用现有自研读取器。
  懒加载语义保留：`open` 只取元数据，`get_slice` 按需读。
- **写**：EDF（pyedflib）、HDF5（h5py）、CSV（pandas），含标注写入（EDF Annotations / HDF5 events / CSV 侧车）。
- **标注导入**：MNE annotations / Intan 事件 / CSV `_markers.csv` 侧车，随 `open` 一并返回。
- **打包**：PyInstaller 打精简边车（`collect_all('mne')`），随安装包内置。
- **生命周期**：主程序负责启动/健康检查/异常重启边车；边车崩溃不拖垮主程序 UI。

## 8. 国际化架构

| 项 | 方案 |
| --- | --- |
| 文案引擎 | Qt `tr()` + Linguist `.ts/.qm`；复杂复数/占位用 ICU MessageFormat |
| 目标语言（按 N 设计） | zh_CN, zh_TW, en_US, ja_JP, ko_KR, de_DE, fr_FR, es_ES |
| 数字/日期/单位 | QLocale/ICU 区域格式化（μV、Hz、s、dB、时间戳）；解决德/法小数逗号 |
| 重翻译 | Qt 的 `changeEvent(LanguageChange)` + `retranslateUi()`，声明式绑定，避免手写遗漏 |
| 错误码本地化 | 边车返回 error_code → 主程序 i18n 层映射（见 §3.3） |
| 完整性校验 | 构建/测试期校验每语言无缺 key、占位符/ICU 参数一致 |
| 伪本地化 | 提供把文本加长 ~30% 的 pseudo-locale，开发期暴露德语撑爆布局问题 |
| RTL | 明确不做 |

## 9. 授权系统（原生）

- 机器码 = SHA256(MachineGuid + MAC) → 32 位大写十六进制，`XXXXXXXX-...` 分组（与现有一致）。
- RSA-2048 PSS 验证（`PSS.AUTO` 等价），密钥 Base64url + `.` 分隔，语义与现有 validator 对齐。
- C++ 用 OpenSSL 或 Botan 实现；密钥文件路径 `%APPDATA%\BoundFreeEEG\license.dat`。
- keygen 保持为独立开发者工具（可继续用 Python），绝不打包进发布版。
- 放原生的理由：Python 边车易被反编译，安全逻辑放原生更抗破解。

## 10. 打包与构建

- **C++**：CMake + vcpkg（Qt6/Eigen/pocketfft/OpenSSL 依赖管理）。
- **边车**：PyInstaller 打包为子目录。
- **安装包**：沿用 Inno Setup 思路，把原生主程序 + 边车 + Qt 运行时 + 法务文件一起封装；
  `DisableDirPage=no` 等现有经验保留。
- **代码签名**：为医疗器械分发预留 Windows 代码签名步骤。

## 11. 功能不变的保证（验收闸门）

采用「黄金文件」回归策略，确保迁移后功能与数值一致：

1. 用现有 Python 版对一批样本文件跑出标准输出：滤波结果、PSD/STFT/coherence/频段功率、
   导出的 EDF/HDF5/CSV。
2. 新 C++ 版对同样输入产出结果，与黄金文件**逐位或在容差内**比对。
3. 建立独立的《功能对齐清单》文档，逐条勾验：
   格式读写、滤波、四个分析 tab、标注、视频同步、录制、导出、回放控制、主题、i18n、授权。
4. 数值容差：滤波/谱分析允许浮点级别小容差（记录容差标准），格式往返允许元数据等价而非逐字节。

## 12. Milestone 0：技术验证 Spike（先做，2–3 天）

正式全量重写前，只验证两个最高风险点，通过再投入：

- **Spike-1 渲染**：C++/Qt `QOpenGLWidget` 渲染 256ch @ 1kHz 合成信号，测帧率与 CPU/GPU 占用，
  验证滚动/缩放/通道显隐流畅度。
- **Spike-2 IPC**：最小 Python 边车 + 主程序，验证 `open`/`get_slice` 往返延迟与大切片吞吐，
  判定二进制管道是否够用、是否需要共享内存。
- **附带产物**：对现有 Python 版做一次性能画像（渲染/DSP/切片IO 各自耗时占比），
  作为后续优化与验收基线。
- **退出判据**：两个 spike 达标 → 进入 Milestone 1；否则回到设计修正。

## 13. 里程碑规划（Milestone 0 之后）

| 里程碑 | 内容 | 交付 |
| --- | --- | --- |
| M1 骨架 | CMake/vcpkg 工程、Qt 主窗口、边车进程管理、IPC 打通、i18n 骨架、授权校验 | 可启动、可激活、可与边车握手 |
| M2 打开与静态浏览 | `open`/`get_slice`、波形 GPU 渲染、通道显隐/时间窗、主题 | 能打开各格式并静态浏览波形 |
| M3 信号处理 | 重参考、陷波（含谐波）、带通、实时/离线路径 + 黄金文件比对 | 滤波数值对齐旧版 |
| M4 谱分析 | PSD/STFT/频段趋势/相干性四 tab，后台计算 | 四分析对齐旧版 |
| M5 回放与标注 | 播放/暂停/seek/倍速、prefill、事件标注与导入 | 回放与标注功能对齐 |
| M6 视频同步 | 视频从属 EEG 时钟的 sync_to 逻辑复刻 | 视频同步对齐 |
| M7 录制与导出 | 经边车双格式录制、背压、片段导出（含标注） | 录制/导出对齐 |
| M8 打包与验收 | PyInstaller 边车 + Inno Setup 安装包、全量功能对齐清单勾验、代码签名 | 可发布安装包 |

## 14. 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| FIF/EEGLAB 自写解析不可行 | 已用 Python 边车永久承接，不自写 |
| 256ch 渲染达不到流畅 | Milestone 0 先验证；自绘 OpenGL + 抽稀兜底 |
| IPC 成为瓶颈 | 先二进制、后共享内存；Milestone 0 量化 |
| 数值不一致 | 黄金文件逐条比对 + 容差标准 |
| 已修 bug 在新语言复现 | 以 AGENTS.md bug 表为清单逐条落地并加测试 |
| 边车崩溃 | 主程序健康检查 + 自动重启 + UI 降级提示 |
| C++ 开发/招人成本 | 已确认愿意投入；工程规范 + 现代 C++ + 静态分析约束 |

## 15. 非目标（本次不做）

- RTL 语言与布局镜像。
- 微服务化、前后端分离、Web 前端。
- BLE 实时采集的完整实现（仅预留接口，硬件未到货）。
- 更换现有授权算法与密钥体系（保持兼容）。

## 16. 与参考仓库的关系

- 旧 Python 仓库 `../boundfree-eeg-upper-computer`（PySide6 版）**降级为参考仓库**，
  不再继续演进，仅用于：(1) 作为 Python 边车文件读写部分的实现基础；
  (2) 作为 DSP/交互行为的参考实现；(3) 作为黄金文件的产生器。
- 本设计**取代**参考仓库 `docs/08-architecture-baseline.md` 中「不进行语言迁移」的旧决策；
  其余分层原则（UI 只管 UI、算法不进 UI、数据 `[n_ch, n_samp]`/μV、视频从属 EEG 时钟等）
  在新架构中**继续沿用**。
