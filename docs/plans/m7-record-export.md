---
title: M7 录制与片段导出实施计划
doc_type: plan
status: completed
owner: BoundFree
last_updated: 2026-07-22
milestone: M7
---

# M7 录制与片段导出实施计划

## Objective

在不接入 BLE 或真实硬件的前提下，补齐 Native 实时数据入口、经 Python 边车双格式录制以及 EDF/HDF5/CSV 片段导出链路：

- 用当前文件回放源模拟实时 EEG chunk，验证后续硬件可复用的数据入口。
- 同时录制 EDF 与 HDF5；写入不阻塞 GUI，队列有界并报告积压与丢弃计数。
- 录制期间追加事件，保证 HDF5 数据与事件由同一写线程、同一文件句柄串行写入。
- 异步导出当前文件的任意合法区间，并按格式写入标注。
- 由边车重新打开导出结果，验证元数据、样本和标注往返一致。
- 产品版本保持 `0.1.0-dev`，IPC 协议版本保持 `1`；M7 在协议版本 1 内补齐尚未发布的预留方法。

## Current State

- M3 已提供 Native DSP；M5 已提供文件回放时钟、seek、倍速、标注与 CSV 标注侧车；M6 已完成视频从属同步。
- `SidecarClient` 和正式边车当前使用逐行 JSON stdio。`get_slice` 把二维浮点数组嵌入 JSON；这与架构文档约定的“长度前缀二进制 EEG payload”不一致，也不适合持续录制。
- M0 spike 已验证长度前缀二进制 stdio 可行，但使用的是临时代码和大端帧头，不能直接进入正式协议。
- 当前文件浏览每次窗口请求都会启动独立边车、打开文件、取得切片后退出；没有可持续产生 chunk 的 Native 数据源，也没有录制会话。
- 边车仅实现 `hello/open/get_slice/close/export_markers_csv`；没有写入器、录制会话或片段导出。
- `sidecar/pyproject.toml` 已声明 `h5py` 和 `mne`，尚未声明 EDF 写入依赖 `pyedflib`。CSV 写入可使用标准库，不引入无必要的 pandas 运行时依赖。
- 旧版 `Recorder` 可作为行为参考，但其满队列停机时会移除一个待写条目以插入 sentinel；Native M7 不复制该数据丢失行为，停止录制必须先拒绝新数据，再排空已接受队列。
- 旧版片段 CSV 不写 `time_s`，无法被当前 `CsvReader` 重新打开；M7 输出必须写相对片段起点的 `time_s` 列。
- 当前 `Hdf5Reader` 不读取 `events/time` 和 `events/label`，无法完成 HDF5 标注往返。

## Architecture

```text
┌────────────────────────── Native / Qt6 ──────────────────────────┐
│                                                                  │
│  当前文件会话 ─► FileReplaySource ─► RecordController            │
│                        固定 chunk       │                         │
│                        单调时钟节拍      │ 有界输入队列            │
│                                         ▼                         │
│                                  RecordWorker(QThread)            │
│                                         │                         │
│  导出区间 + MarkerStore ─► ExportController(QThread)             │
│                                         │                         │
└─────────────────────────────────────────┼─────────────────────────┘
                                          │
                          小端长度前缀 JSON / float32 帧
                                          │
┌──────────────────────── Python Sidecar ─▼────────────────────────┐
│                                                                  │
│  RecordingRegistry ─► 单写线程 ─┬─► EDF                          │
│                                 └─► HDF5 eeg + events             │
│                                                                  │
│  export_segment ─► Reader.get_slice ─┬─► EDF + Annotations       │
│                                     ├─► HDF5 + events            │
│                                     └─► CSV + _markers.csv       │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 边界与不变量

- Native 只协调数据源、状态、背压和 UI；科研文件编码全部留在边车。
- 原始录制数据固定为 `[n_channels, n_samples]`、连续 C-order、`float32`、小端、单位 `uV`。
- 每个 Native `SidecarClient` 同时最多有一个未完成请求；录制和导出使用各自独立的边车进程，避免长写入阻塞文件浏览。
- Native 输入队列和边车写队列都必须有明确上限；分别报告 `source_dropped_chunks` 与 `writer_dropped_chunks`，不得合并成无法定位的单一计数。
- 队列满时丢弃最新 chunk，不覆盖已接受数据。事件与数据进入同一边车写队列，保持单写句柄；事件满队列时单独累计 `dropped_events`。
- `record_close` 先停止接收，再排空已接受条目并关闭写入器；超时或写线程失败返回结构化错误，不把未关闭文件报告为成功。
- 录制事件时间和导出标注时间统一使用相对各自文件起点的非负秒值。导出时筛选源区间 `[t0, t1]` 内标注并减去 `t0`。
- M7 文件回放源按固定 chunk 顺序读取并由单调时钟节拍发送，不复用 UI 可见窗口或 prefetch 请求，避免 seek、重叠窗口和 latest-request-wins 造成录制重复或缺口。

## Impacted Files

### 契约与依赖

- 修改 `docs/contracts.md`：固定帧格式、payload 顺序、录制/导出请求响应、时间与错误语义。
- 修改 `contracts/error_codes.json`：增加 `record.*` 与 `export.*` 错误码。
- 修改 `sidecar/pyproject.toml`：在格式依赖中增加固定版本 `pyedflib`；CSV 继续使用标准库。

### Python 边车

- 新增 `sidecar/boundfree_sidecar/framing.py`：小端长度前缀帧读写、帧类型、最大 payload 限制和短读处理。
- 修改 `sidecar/boundfree_sidecar/stdio.py`：从文本逐行 JSON clean cutover 到二进制帧循环。
- 修改 `sidecar/boundfree_sidecar/protocol.py`：返回 envelope + 可选 payload，接入录制与导出方法。
- 新增 `sidecar/boundfree_sidecar/writers/recording.py`：双格式录制、单写线程、有界队列、事件和关闭语义。
- 新增 `sidecar/boundfree_sidecar/writers/export.py`：三格式片段导出和标注写入。
- 修改 `sidecar/boundfree_sidecar/readers/hdf5_reader.py`：读取统一 `events/time`、`events/label` 结构。
- 修改 `sidecar/boundfree_sidecar/readers/csv_reader.py`：保持 `time_s` 输入契约并覆盖 M7 导出结果。

### Native

- 修改 `native/src/ipc/sidecar_client.{h,cpp}`：正式帧协议、二进制切片、录制和导出 API。
- 新增 `native/src/controller/record_controller.{h,cpp}`：录制状态、文件回放源、有限队列、工作线程、计数和关闭。
- 新增 `native/src/controller/export_controller.{h,cpp}`：区间校验、独立工作线程、忙碌/完成/失败信号。
- 新增 `native/src/ui/record_panel.{h,cpp}`：元数据输入、目录选择、开始/停止、REC、耗时、背压和低磁盘提示。
- 修改 `native/src/ui/main_window.{h,cpp}`：接入录制面板、当前文件元数据、MarkerStore 事件和片段导出入口。
- 修改 `native/src/app/main.cpp`：控制器接线与 M7 smoke 命令。
- 修改 `native/CMakeLists.txt`：登记新增源码。
- 修改 `native/i18n/bf_app_*.ts`：所有录制、导出、状态和错误文案同步九个 locale；`zh_CN/en_US/pseudo` 提供实际文案，其余 locale 保持 key 完整。

### 验证与收口

- 新增 `tests/sidecar/test_m7_framing.py`。
- 新增 `tests/sidecar/test_m7_recording.py`。
- 新增 `tests/sidecar/test_m7_export.py`。
- 新增 `tests/native/test_m7_record_export_smoke.py`。
- 新增 `tests/golden/m7/` 中可审计的小型输入与旧版期望元数据；不提交临时录制输出。
- 验收通过后新增 `docs/reports/m7-record-export-acceptance.md`，并更新 `docs/roadmap.md`、`docs/feature-parity-checklist.md`、`README.md` 与 `AGENTS.md` 的当前阶段。

## Changes

### 1. 固定正式 IPC 帧协议

1. 将 stdio clean cutover 为统一帧：`u8 frame_type + u32le payload_length + payload`。
2. 定义 `frame_type=1` 为 UTF-8 JSON，`frame_type=2` 为原始二进制；拒绝未知类型、超限长度和短帧。
3. 普通请求只交换 JSON 帧。
4. `get_slice` 成功响应先发送 JSON 元数据帧，再发送一个 `float32-le` 数据帧；Native 按 `shape` 和 `payload_bytes` 校验后直接构造通道矩阵。
5. `record_write` 请求先发送 JSON 参数帧，再发送一个数据帧；边车必须在读下一个请求前消费并校验 payload。
6. 保留请求 envelope、request id、结构化错误和同步单请求模型；不保留逐行 JSON 兼容分支。
7. 为帧和数组设置上限，默认单 payload 不超过 256 MiB，并在乘法计算前检查 shape 溢出。

### 2. 实现边车录制会话

1. 实现 `record_open`：校验 `fs/ch_names/out_dir/meta/formats/max_queue_chunks`，创建唯一 handle，并启动单写线程。
2. EDF 使用 `pyedflib`；通道名按 EDF 限制处理，单位为 `uV`，保持旧版物理/数字范围和元数据语义。
3. HDF5 创建 `eeg`：`float32`、`shape=(n_channels, 0)`、`maxshape=(n_channels, None)`、按约一秒样本分块；写入 `fs/ch_names/unit` 和 `meta`。
4. HDF5 事件统一为 `events/time: float64` 与 `events/label: UTF-8` 两个可扩展数据集，不继续使用旧版二维字符串数据集。
5. 实现 `record_write`：严格校验 handle、shape、通道数、有限浮点值与 payload 长度；非阻塞入队，返回 backlog、累计写入样本与 writer 丢弃计数。
6. 实现 `record_event`：校验非负有限秒值和 label，进入同一写队列。
7. 实现 `record_close`：停止接收、排空、关闭、返回 EDF/HDF5 路径、样本数和全部计数；重复关闭返回 session not found。
8. 将后台异常保存到会话，后续 write/event/close 返回 `record.writer_failed`，且携带稳定结构化参数。

### 3. 建立 Native 录制边界

1. `RecordController` 暴露 `startFromFile`、`submitChunk`、`appendEvent`、`stop`，以及状态/统计信号。
2. 使用明确状态：`Idle → Starting → Recording → Stopping → Idle`；任一步骤失败进入 `Failed`，清理后回到 `Idle`。
3. 文件回放源从当前文件和当前 EEG 位置开始，按固定样本数顺序读取；每个源样本最多提交一次。
4. 回放源按单调时钟节拍，不受 UI seek、倍速和视频同步影响；这是录制链路测试源，不冒充真实 BLE 采集。
5. Native 有界输入队列在调用线程快速接受或拒绝 chunk；实际 IPC 在工作线程执行，GUI 不等待磁盘或 Python。
6. 录制时新增 MarkerStore 事件调用 `appendEvent(current_record_time, label)`；开始录制前已有标注不自动写入录制文件。
7. 停止、切换文件和窗口关闭走同一幂等停止路径；禁止析构时遗留写线程或边车进程。

### 4. 接入录制 UI

1. 元数据对话框收集 `animal_id/exp_type/notes/output_dir`；输出目录必填且必须可写。
2. 无当前 EEG 文件、正在停止或已有录制会话时拒绝开始。
3. 显示 REC 闪烁、单调计时、source/writer backlog 与丢弃计数。
4. 每秒读取输出目录剩余空间；低于 500 MiB 显示结构化警告，不自动终止录制。
5. 录制错误通过 error code → i18n 呈现；第三方异常只进入 `reason` 参数和日志上下文。

### 5. 实现三格式片段导出

1. `ExportController` 校验当前源路径、`0 <= t0 < t1 <= duration`、输出扩展名和非忙碌状态。
2. 专用工作线程和专用边车进程调用 `export_segment`；UI 只处理 busy/done/failed。
3. 边车重新打开源 reader 并读取一次目标切片，不依赖短生命周期 file handle。
4. CSV 写 `time_s` 加通道列；有片段内标注时写 `{stem}_markers.csv`。
5. HDF5 写 `eeg`、`fs/ch_names/unit` 和可选 `events/time`、`events/label`。
6. EDF 写通道数据并调用 annotations；标注时间相对导出片段起点。
7. 成功响应返回输出路径、样本数、通道数和 marker_count；不支持扩展名、非法区间、源读取失败和目标写入失败使用独立 `export.*` 错误码。
8. 同一窗口只允许一个导出任务；M7 不提供无法保证原子性的“假取消”按钮。

### 6. 补齐导出结果回读

1. HDF5 reader 读取统一事件数据集，验证 time/label 长度一致、时间有限且非负。
2. CSV reader 重新打开 M7 输出，采样率由 `time_s` 推导；单样本片段按协议返回明确采样率元数据，避免默认为 1 Hz。为此 CSV 导出可写伴随元数据或拒绝不足两样本的 CSV 区间，实施时选择与现有 reader 最小兼容的方案并在契约中固定。
3. EDF 继续通过 MNE annotations 回读。
4. 比较时 HDF5/CSV 使用 float32 容差；EDF 按物理/数字范围产生的量化步长设置容差，不要求逐字节一致。

### 7. 收口文档与阶段状态

1. 记录依赖版本、环境、命令、样本、容差、积压上限和丢弃策略。
2. 只有三格式回读、双格式录制和 GUI 非阻塞 smoke 均通过后，才将计划状态改为 completed 并勾选 M7 清单。
3. M7 完成后路线图下一阶段改为 M8；不提前宣称真实设备录制或发布安装包完成。

## Error Contract

计划新增以下错误码，实施时同步 C++、Python、i18n 和注册表：

- `ipc.invalid_frame`：帧类型、长度、短读或 payload 与元数据不一致。
- `record.invalid_state`：当前状态不允许操作。
- `record.session_not_found`：录制 handle 不存在。
- `record.open_failed`：输出目录或写入器初始化失败。
- `record.invalid_chunk`：shape、通道数、dtype 或数值非法。
- `record.writer_failed`：后台写线程失败。
- `record.close_timeout`：已接受队列未在关闭期限内排空。
- `export.invalid_range`：导出区间非法。
- `export.unsupported_format`：目标扩展名不受支持。
- `export.busy`：已有导出任务。
- `export.failed`：源读取或目标编码失败。

所有错误继续使用 `code + params + retryable + trace_id`，不得返回已本地化完整句子。

## Risks

- **帧协议切换影响全部边车调用**：一次迁移 `hello/open/get_slice/close/export_markers_csv` 和相关 smoke，不保留双协议解析器；先完成协议测试，再实现录制。
- **双层队列导致统计混淆**：Native 与 writer 丢弃计数分字段呈现；验收分别压满两个边界，禁止只测总数。
- **EDF 写入量化或越界**：固定并记录物理范围；黄金比较按量化步长，越界行为必须测试，不能静默把裁剪结果当原值。
- **停止时数据丢失或死锁**：关闭顺序固定为拒绝新数据、停止源、排空 Native 队列、关闭边车会话、结束线程；每层有期限和错误结果。
- **边车后台异常不可见**：写线程异常进入会话状态，任何后续请求都返回同一稳定错误；close 不能覆盖失败。
- **磁盘耗尽产生半文件**：写失败必须关闭全部句柄并报告失败路径；验收记录半文件处理策略，UI 不报告成功。
- **文件回放不等于真实采集**：报告只证明数据源边界、背压和写盘链路，不外推 BLE、无线稳定性或 12 小时长稳。
- **依赖许可证与打包**：`pyedflib` 版本与 BSD 许可在 M7 记录；PyInstaller 收集和最终法务材料仍属于 M8。

## Validation

### 协议与边车

- 帧读写覆盖分段读取、空 payload、未知类型、超限、短帧、错误 shape、大小端和连续多帧。
- `hello/open/get_slice/close/export_markers_csv` 在新帧协议下全部回归。
- 录制覆盖 EDF+HDF5 同时生成、按秒 chunk、float32、maxshape、元数据、事件、队列满、writer 异常和排空关闭。
- 导出覆盖三种格式、区间边界、标注筛选/重基准、非法扩展名和目标不可写。

### Native 与 UI

- `bf_app --smoke-m7-record <fixture> <out_dir>`：文件回放 chunk 经 Native 队列和二进制 IPC 生成 EDF/HDF5，返回计数一致。
- `bf_app --smoke-m7-export <fixture> <out_dir>`：异步导出三格式，GUI 定时器在导出期间持续 tick，证明主线程未阻塞。
- offscreen UI smoke 覆盖无文件拒绝、开始/停止、REC/耗时、标注追加、导出 busy 和结构化错误展示。
- 窗口关闭与切文件期间停止录制不遗留线程或边车进程。

### 往返与黄金比对

- 重新 `open/get_slice` 录制和导出文件，比较 fs、通道、样本数、单位、数据和标注。
- HDF5/CSV 数值按 float32 舍入容差比较；EDF 容差由信号头量化步长计算并写入验收报告。
- 使用旧版相同输入生成基准；比较语义与数据，不复制旧版 CSV 缺少 `time_s` 和关闭时主动丢队列项的问题。

### 回归命令

实施完成后至少运行：

```text
cmake -S native -B native/build
cmake --build native/build -j --target bf_app m3_dsp_test m4_spectral_test
native/build/m3_dsp_test tests/golden/m3
native/build/m4_spectral_test tests/golden/m4
PYTHONPATH=sidecar python3 -m pytest tests/sidecar -q
QT_QPA_PLATFORM=offscreen python3 -m pytest tests/native -q
python3 scripts/common/check_i18n.py
python3 -m json.tool contracts/error_codes.json
```

另执行 M7 两个真实 smoke，并把实际命令、环境、输出、依赖版本和容差写入 `docs/reports/m7-record-export-acceptance.md`。

## Out of Scope

- BLE、自研 `ADS1299-4 + ESP32-C3` 协议和真实设备状态。
- 真实设备录制、自动重连、无线丢包统计和 12 小时长稳。
- PyInstaller 边车、Inno Setup、Windows 代码签名和发布版 Qt/FFmpeg 法务收口。
- 录制暂停/续录、跨文件滚动、云同步、远程控制和多设备录制。
- 在 M7 引入共享内存；只有二进制 stdio 的实测吞吐成为瓶颈时另立性能门禁。
