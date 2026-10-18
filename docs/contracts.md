---
title: 基础契约
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-10-18
---

# 基础契约

本文集中定义 BoundFree EEG Native 的版本源、IPC envelope、错误码和 i18n 约束。M1-A 起新增代码必须遵循这些契约。

## Version Source

根目录 `version.json` 是唯一人工维护版本源。

```json
{
  "schema_version": 1,
  "product_name": "BoundFree EEG",
  "version": "0.1.0-dev",
  "release_channel": "dev",
  "ipc_protocol_version": 1
}
```

字段含义：

| 字段 | 规则 |
| --- | --- |
| `schema_version` | `version.json` 自身结构版本，初始为 `1` |
| `product_name` | 程序显示名和安装包显示名 |
| `version` | 产品版本，遵循 SemVer |
| `release_channel` | `dev`、`alpha`、`beta`、`rc`、`stable` |
| `ipc_protocol_version` | 主程序与边车协议版本，独立于产品版本 |

M1-A 需要消费该文件：

- `bf_app --version` 输出产品版本。
- 主窗口显示产品版本。
- Python 边车 `--version` 和 `hello` 返回产品版本与 `ipc_protocol_version`。

## IPC Envelope

控制消息使用 JSON envelope，传输统一使用长度前缀二进制帧。已实现 `hello/open/get_slice/close/export_markers_csv/record_open/record_write/record_event/record_close/export_segment/export_samples`。

请求 envelope：

```json
{
  "id": "request-1",
  "method": "hello",
  "params": {}
}
```

成功响应：

```json
{
  "id": "request-1",
  "ok": true,
  "result": {
    "product_version": "0.1.0-dev",
    "ipc_protocol_version": 1
  }
}
```

失败响应：

```json
{
  "id": "request-1",
  "ok": false,
  "error": {
    "code": "protocol.unknown_method",
    "params": {
      "method": "bad_method"
    },
    "retryable": false,
    "trace_id": "20260706-000001"
  }
}
```

帧格式：`u8 frame_type + u32le payload_length + payload`。

- `frame_type=1`：UTF-8 JSON。
- `frame_type=2`：原始二进制 payload。
- 单 payload 最大 `256 MiB`；未知类型、短帧、超限和元数据不匹配返回 `ipc.invalid_frame`。
- `id` 由主程序生成，响应必须原样返回。
- `method` 是稳定协议名，不参与本地化。
- `params` 只放结构化数据，不放用户可见完整句子。
- `ok=false` 时必须包含 `error.code`。
- `trace_id` 用于日志和售后定位，UI 默认不把它当正文文案。
- EEG payload 使用小端连续 C-order `float32`，形状为 `[n_channels, n_samples]`，单位为 `uV`。
- `get_slice` 成功时先返回 JSON 元数据帧，再返回一个二进制数据帧。
- `record_write` 先发送 JSON 参数帧，再发送一个二进制数据帧。

M5 `export_markers_csv`：请求包含 `path` 和 `{time,label}[]`；写入 `{stem}_markers.csv`，列为 `time_s,label`。

M7 录制：

- `record_open`：`fs/ch_names/out_dir/meta/formats/max_queue_chunks`，返回 `handle/paths`。
- `record_write`：`handle/shape` 加二进制 chunk，返回 `accepted/backlog_chunks/samples_written/dropped_chunks/dropped_events`。
- `record_event`：事件时间是相对录制起点的非负秒值，事件与 EEG 数据进入同一边车写队列。
- `record_close`：停止接收并排空已接受数据，返回路径和最终统计。

M7 `export_segment`：请求包含 `src/t0/t1/path/markers`；标注筛选源区间 `[t0,t1]` 并减去 `t0`。成功返回路径、通道数、样本数和标注数。

`export_samples`：Native 处理后的片段写入接口，先发送 JSON 参数，再发送一个二进制数据帧。

- 参数为 `path/fs/ch_names/shape/dtype/unit/t0/t1/markers`；`dtype="float32-le"`、`unit="uV"`，payload 为通道优先的连续小端 float32。
- `shape` 为两个正整数，第一维必须与 `ch_names` 长度一致，payload 字节数必须为两维乘积的 4 倍；元数据与 payload 不匹配返回 `ipc.invalid_frame`。
- `fs` 必须为有限正数，样本必须为有限值，时间范围满足 `0 <= t0 < t1`。格式与 `export_segment` 一致，支持 EDF/HDF5/CSV。
- `t0/t1/markers` 使用源文件时间坐标，写入时按同样规则筛选标注并减去 `t0`。成功响应与 `export_segment` 相同。
- Native 承担重参考、陷波及逐通道带通，边车只编码收到的样本，不重复实施 DSP。通道名使用处理后的输出名称；显隐、幅度缩放和绘图抽稀不进入导出数据。
- 该接口为增量新增方法，既有 envelope、帧格式及 `export_segment` 行为保持兼容，`ipc_protocol_version` 保持 `1`。处理后导出要求主程序与边车一同更新。

## Error Codes

错误码是协议契约。后端和边车只返回错误码与参数，UI 通过 i18n 映射为用户文案。

`contracts/error_codes.json` 是错误码注册表。新增、替换或调整错误码参数时，必须先更新该注册表，再同步 C++、Python 和 i18n 文案。

命名规则：

- 使用小写英文、数字、下划线和点号。
- 采用命名空间：`protocol.*`、`ipc.*`、`sidecar.*`、`file.*`、`reader.*`、`license.*`、`analysis.*`、`record.*`、`export.*`、`internal.*`。
- 不把具体第三方异常类名暴露为错误码。
- 不删除已公开错误码；需要替代时新增错误码并保留旧码兼容期。

M1-A 初始错误码：

| 错误码 | 参数 | retryable | 用途 |
| --- | --- | --- | --- |
| `protocol.invalid_json` | 无 | `false` | 请求 JSON 无法解析 |
| `protocol.missing_field` | `field` | `false` | 请求缺少必填字段 |
| `protocol.unknown_method` | `method` | `false` | 未知 IPC method |
| `protocol.version_mismatch` | `expected`, `actual` | `false` | 主程序与边车 IPC 协议版本不一致 |
| `ipc.sidecar_start_failed` | `reason` | `true` | 主程序无法启动边车 |
| `ipc.sidecar_timeout` | `operation`, `timeout_ms` | `true` | 边车请求超时 |
| `ipc.sidecar_exited` | `exit_code` | `true` | 边车异常退出 |
| `sidecar.initialization_failed` | `reason` | `true` | 边车初始化失败 |
| `internal.unexpected` | `operation` | `false` | 未分类异常兜底 |

M1-B 扩展：

- 参数校验：`protocol.invalid_params`。
- 文件会话：`file.*`、`reader.*`。
- 授权骨架：`license.*`。

后续阶段按功能扩展：

- 具体读取器：`hdf5.*`、`csv.*`、`rhs.*`、`bbk.*`。
- 分析：`analysis.*`。
- 录制和导出：`record.*`、`export.*`。

M4 扩展：

- `analysis.invalid_input`：分析数据、采样率或窗口参数无效。
- `analysis.invalid_channel`：请求的分析通道不存在。
- `analysis.same_channel`：MSC 相干性必须选择两个不同通道。
- `analysis.failed`：未分类的 Native 分析失败。

M7 扩展：`ipc.invalid_frame`、`record.invalid_state`、`record.session_not_found`、`record.open_failed`、`record.invalid_chunk`、`record.writer_failed`、`record.close_timeout`、`export.invalid_range`、`export.unsupported_format`、`export.busy`、`export.failed`。

## i18n Contract

i18n 采用 Qt6 Linguist `.ts/.qm` 双轨制：

- `.ts` 仅为翻译源文件（开发期维护、静态检查、lrelease 输入），**不作为运行时资产**，不进入发布包。
- `.qm` 是唯一运行时翻译资产，由 CMake `qt_add_lrelease` 在构建期从 `.ts` 编译，`cmake --install` 安装到 `i18n/`。运行时缺失 `.qm` 即失败，**不回退 `.ts`**。

v1 stable 必达语言（stable 用户菜单暴露这三项）：

- `zh_CN`
- `en_US`
- `ja_JP`

仅测试用、不进 stable 用户菜单、不进发布包但保留 `.ts` 源以供构建：

- `pseudo`

以下 locale 保留 `.ts` 源文件用于架构扩展和 key 完整性检查，但不产出 `.qm`、不进发布包、不作为 stable 验收项：

- `zh_TW`
- `ko_KR`
- `de_DE`
- `fr_FR`
- `es_ES`

构建期仅对 `zh_CN`、`en_US`、`ja_JP`、`pseudo` 执行 lrelease 产出 `.qm`；其余 locale 不产出 `.qm`。

资源查找顺序（`.qm` 必须命中其一，缺失即失败）：

1. `$BF_RESOURCE_ROOT/i18n/bf_app_<locale>.qm`
2. `<应用可执行目录>/i18n/bf_app_<locale>.qm`（发布布局：stage 根即应用目录）
3. `<repoRoot>/native/i18n/bf_app_<locale>.qm`（开发构建产物，与 CMake lrelease 输出一致）
4. `<repoRoot>/i18n/bf_app_<locale>.qm`（向后兼容布局）

Qt 基础翻译（`qtbase_*.qm`）按同一顺序可用即安装，缺失不视为错误；发布包随附 `qtbase_zh_CN.qm`（强制）、`qtbase_ja.qm`（强制，Qt6 仅提供 language-only `qtbase_ja.qm`，发布重命名为 `qtbase_ja_JP.qm` 以匹配运行时按 `<locale>` 查找路径）与 `qtbase_en.qm`（按存在复制，en_US 运行通常无需 translator）。

应用翻译 context 固定为 `app`；`I18nService::text(key, params)` 对该 context 调用 `QTranslator::translate`，缺失 key 返回 key 本身，并对 `params` 做 `{name}` 占位符替换。

启动优先级（不持久化的解析，仅决定本次运行语言）：

1. CLI 覆盖（`--language`，仅作用于本次运行，不写 QSettings）
2. 已保存设置（仅当属 stable locale 才采纳）
3. `QLocale::system()` 对 stable 集合的最佳匹配
4. 兜底 `zh_CN`

持久化语义：

- CLI 覆盖不持久化。
- 用户菜单切换语言经 `setLocale` 持久化到 QSettings（仅 stable locale 可写入）。
- `loadLocale` 为非持久化加载（CLI、启动初始化、测试、pseudo），不写 QSettings。

规则：

- 默认语言为 `zh_CN`。
- 常驻控件必须支持 `retranslateUi()`，并在 `languageChanged` 信号下重译。
- 错误文案 key 使用 `error.<code>`，例如 `error.protocol.unknown_method`。
- 翻译占位符必须与错误码参数一致。
- 不翻译通道名、文件路径、用户标注、实验备注、CSV/HDF5/EDF schema、IPC method、内部枚举。
- 边车和后端不得返回用户可见完整句子。

## Static Checks

M1-A 需要建立静态检查，覆盖：

- 翻译 key 完整性。
- 翻译占位符一致性。
- 错误码均有 `error.<code>` 文案。
- UI 代码不新增硬编码中文。
- 翻译资源不包含文件扩展名、schema 字段、IPC method 这类不可翻译内容。

## 回放视窗与读片缓存

阶段 2B 起，静态切片范围和播放显示范围分离：

- UI 时间窗仍为 1–120 秒。内部预取读取当前窗口加 2–8 秒前瞻，单请求上限 128 秒；不改变 get_slice 协议或单位。
- WaveformWidget 的 setTimeRange/clearTimeRange 只改变可见时间范围，setSlice 更新数据缓存且不清除视窗。采样时间为 t0 + i/fs，t1 是右开边界。
- 播放采用前向窗口 [position, position+window]，末段起点钳制到 duration-window；光标仍表示真实播放位置。暂停保留该视窗，暂停后定位使用返回的静态切片范围。
- 每个 FileOpenController 只有一个在途操作与一个可覆盖 pending 请求。同文件复用 handle，显式重开和换文件关闭旧 handle；结果由 generation 隔离。
- 数据不足覆盖下一视窗时冻结时间推进并暂停从属视频；补齐后按原播放意图续播，不累计等待时间后补跳。读取失败或预取结果不足覆盖时停止播放。
- 波形每帧只平移视窗，收到新缓存才处理数据；播放分析至少间隔 500ms，输入裁剪为实际可见样本。保持现有离线零相位滤波方式，尚未切换到有状态实时滤波。
- 缓存按整块覆盖，不累积历史块；上限是时间范围，不是固定字节预算。高采样率/多通道的实际桌面性能需单独验收。

## 波形幅值与逐通道参数

波形区提供“原始波形 / 处理后波形”选择，默认处理后；选择同时决定波形与频谱分析的输入。原始模式跳过重参考与滤波，保留处理设置，原始通道名称和通道数随之恢复。处理模式继续使用离线零相位处理链。原始模式下逐通道带通编辑区禁用，避免将源通道索引误用于双极等参考输出。

导出对话框集中设置区间和数据来源，默认范围为当前可见区间、默认来源跟随波形；用户可以独立更改导出来源。打开对话框时捕获处理参数和当前缓存范围。处理后导出在工作线程读取、处理、写入；区间位于捕获缓存中时，先处理整个缓存上下文再裁剪，以对齐所显示的滤波结果，否则以用户选择的片段作为滤波上下文。时间到样本的换算遵循读取器的 ties-to-even 舍入。

处理后导出暂使用单切片和单二进制 payload，仍受 `256 MiB` 单帧上限约束，尚非任意长度的流式整文件滤波导出。直接使用原始导出不启用新的 Native 处理路径。EDF 继续遵循原有量化与记录填充规则。

自动幅值是显示变换，不修改样本或分析输入。范围取当前可见时间窗中的有限值，各通道独立；手动模式仍使用统一 µV 半幅。通道纵向滚动不改变时间窗口和标注时间。

逐通道带通覆盖 map 缺项表示继承全局，`enabled=false` 表示显式关闭。覆盖按参考后的输出索引应用，双极输出 i 对应源 i 减源 i+1。切换参考保留覆盖，新文件清除覆盖与未提交草稿；全局设置保留。逐行非法输入不写入处理状态，必须修正或明确取消。
