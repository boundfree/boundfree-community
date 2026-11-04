# capture.v1 边车接口交接

本接口沿用现有 `id/method/params` 请求、`id/ok/result|error` 响应及 stdio 帧（`u8 type + u32le length + payload`），IPC 版本不变。所有请求在同一 stdio 连接中串行执行。以下 ID 和 handle 是示例；以下 `stream` 中所有身份字段均必须逐项匹配已打开描述。`generation` 和所有 u64/i64 值为十进制字符串，`clock_id` 可以为 0。每个块最多 1 MiB 原始样本字节，channel-major，单一 dtype；payload 以外不附加填充。

## 能力协商

请求 `{"id":"h","method":"hello","params":{"requested_capabilities":["capture.v1"]}}`。若依赖 numpy/h5py 可用，响应 `result` 包含 `"capabilities":["capture.v1"]`；不可用则为 `[]`。没有请求已知采集能力时，原有 hello 字段和值保持不变。

调用方需要保存非空采集来源元数据时，请求 `requested_capabilities:["capture.v1","capture.metadata.v1"]`，并在打开前要求响应同时包含两项；缺少元数据能力时以 `capture.unavailable` 和 `dependency:"capture.metadata.v1"` 失败。边车只返回请求过的已知能力，依赖不可用时返回空数组。仅请求 `capture.v1` 的旧调用响应不变；仅支持 `capture.v1` 的旧边车不能证明自己保存了新增元数据，不能将其用于带元数据的录制。Native 元数据为空时仍只请求 `capture.v1`。

## 打开

```json
{"id":"o","method":"capture_open","params":{
  "out_dir":"/tmp/example",
  "host_clock":{"source":"monotonic","boot_id":"host-boot-id","monotonic_base_ns":"10000"},
  "acquisition_metadata":{"rate_requested":"250","rate_readback":"unknown","gain_origin":"declared","vref_origin":"unknown","start_boundary":"unknown","end_boundary":"unknown"},
  "streams":[{
    "device_id":"device-1","boot_id":"11111111-1111-4111-8111-111111111111",
    "session_id":"22222222-2222-4222-8222-222222222222","capture_id":"33333333-3333-4333-8333-333333333333",
    "generation":"1","stream_id":1,"descriptor_revision":1,
    "name":"EEG","signal_type":"eeg","rate_num":1000,"rate_den":1,
    "dtype":"int24","layout":"channel_major","max_samples_per_block":256,"record_start_sample_index":"0",
    "channels":[{"channel_id":"ch1","name":"Ch 1","signal_type":"eeg","unit":"uV","scale":0.1,"offset":0}],
    "clock":{"clock_id":0,"ticks_per_second_num":"1000000","ticks_per_second_den":"1","source":"hardware","epoch":"boot_relative"}
  }]
}}
```

成功的 `result` 为 `{"handle":"<生成的UUID>","path":"<out_dir>/capture-<UUID>","streams":[{"device_id":"device-1","boot_id":"11111111-1111-4111-8111-111111111111","session_id":"22222222-2222-4222-8222-222222222222","capture_id":"33333333-3333-4333-8333-333333333333","generation":"1","stream_id":1,"descriptor_revision":1}]}`。传入的流描述完整写入 manifest。初始 `manifest.finalized=false`；`out_dir` 须已存在。多流按 `(session_id,stream_id)` 唯一；每个 session 中相同 clock_id 的时钟定义必须相同。

`acquisition_metadata` 可省略；提供时必须是 JSON 对象，键和值均为字符串，不接受布尔值、数字、null、数组或嵌套对象。最多 64 项，每个键最多 128 字节、每个值最多 2048 字节，所有键和值的 UTF-8 字节数总和最多 16384 字节；仍受整个 `capture_open` JSON 原有大小限制。空对象有效，省略时清单和检查结果均不添加此字段。Native 的 `CaptureDescriptor.metadata` 为空时省略该参数，否则按录制启动时的快照传入；非法 UTF-8 会被拒绝。

此字段原样写入根级 `manifest.acquisition_metadata`，可记录采样率请求及读回、增益和 Vref 来源、索引含义与未知边界等。字段只记录调用方提供的声明，边车不会推断或确认设备读回、数据真实性或采集边界。不得传入凭证；以下键名按大小写不敏感的精确匹配拒绝：`password`、`wifi_password`、`token`、`access_token`、`refresh_token`、`api_key`、`secret`、`client_secret`、`private_key`、`authorization`、`credential`、`credentials`。该检查不能识别其他名字或字符串值中的凭证，调用方须自行避免传入。

每流 `record_start_sample_index` 是创建录制消费者时原始采集流的起点快照，u64 十进制字符串；省略时默认为 `"0"`。边车从这个起点判断首块是否存在缺口，并将此值显式写入 manifest；不会重设或改写原始采样序号。进行中的采集必须由 Native 提供准确快照，否则会错误报告缺口。

## 追加块

先发送 JSON 帧，**紧接着**发送一个 binary 帧；下面 payload 为 int24 二补码小端，两个样本 -1 和 +1：`ff ff ff 01 00 00`。`first_timestamp_ticks=null` 表示没有有效采样时间，相应 `time_flags=0`；下面例子为有效硬件时间。

```json
{"id":"a","method":"capture_append","params":{
  "handle":"<打开时返回的handle>",
  "stream":{"device_id":"device-1","boot_id":"11111111-1111-4111-8111-111111111111","session_id":"22222222-2222-4222-8222-222222222222","capture_id":"33333333-3333-4333-8333-333333333333","generation":"1","stream_id":1,"descriptor_revision":1},
  "dtype":"int24","shape":[1,2],"sample_count":2,"first_sample_index":"0",
  "first_timestamp_ticks":"123456","host_received_monotonic_ns":"12500000",
  "clock_id":0,"time_flags":1,"quality_flags":0
}}
```

成功 `result`：`{"accepted":true,"duplicate":false,"accepted_samples":"2","written_samples":"2","flushed_samples":"0","durable_samples":null,"last_accepted_sample_exclusive":"2","gap_records":"0","duplicate_blocks":"0"}`。`accepted_samples`、`written_samples` 和 `flushed_samples` 是该流的每通道样本数；`gap_records` 是缺口记录数，`duplicate_blocks` 是重复块数。flush 计数可因后台 flush 已发生而大于 0；`durable_samples` 永远为 null。完整重复块会返回 `accepted:false, duplicate:true`，其他重叠／逆序返回参数错误。下一个样本序号大于预期时记录缺口，不填零。有效身份但含非有限浮点的块会被拒绝，并记录其已知样本区间和不完整状态。其他类型的拒绝，调用方根据实际情况提交 gap 事件或最终丢失摘要。

## 事件和时钟映射

`stream` 使用上述完整身份。事件和映射均为 JSON，不发送 binary 帧。

```json
{"id":"e","method":"capture_event","params":{"handle":"<handle>","record_type":"event",
 "stream":{"device_id":"device-1","boot_id":"11111111-1111-4111-8111-111111111111","session_id":"22222222-2222-4222-8222-222222222222","capture_id":"33333333-3333-4333-8333-333333333333","generation":"1","stream_id":1,"descriptor_revision":1},
 "record":{"event_id":"44444444-4444-4444-8444-444444444444","kind":"data.gap","sample_index":null,"count":null,"params":{"reason":"disconnect","detection":"adapter"}}}}
```

`sample_index` 与 `count` 可为十进制字符串或 null；`data.gap` 必须带上述 `reason/detection`、完整 stream。一般事件可以 `stream:null` 表示全局事件。`record_type:"clock_mapping"` 同样要求完整 stream，`record` 示例：`{"clock_id":0,"mapping_revision":1,"valid_from_ticks":"123456","valid_to_ticks":null,"host_monotonic_ns":"12500000","offset_ns":"-100","uncertainty_ns":null}`；可附加映射和同步测量字段，原样记入 `clock-mappings.jsonl`。成功 `result={"accepted":true}`。

## 结束和清算

调用方须先关闭自己的录制入队，并将已接纳块依次发送完毕。`last_accepted_sample_exclusive` 须与边车最后接收边界严格相等（零块时等于该流 `record_start_sample_index`），不等时拒绝并允许修正后重试。`terminal_sample_exclusive` 是录制终点；若未知填 `null`，不是猜测值。终点大于录制边界时记录尾部缺口。`complete` 表示调用方确认该流在本次录制区间内完整结束，实际文件完整性还会合并缺口、`dropped_samples`、`unknown_gap`；独立停止录制时，设备采集可以继续。所有流均须列出。

```json
{"id":"f","method":"capture_finalize","params":{"handle":"<handle>","streams":[{
  "stream":{"device_id":"device-1","boot_id":"11111111-1111-4111-8111-111111111111","session_id":"22222222-2222-4222-8222-222222222222","capture_id":"33333333-3333-4333-8333-333333333333","generation":"1","stream_id":1,"descriptor_revision":1},
  "last_accepted_sample_exclusive":"2","terminal_sample_exclusive":"2","complete":true,"dropped_samples":"0","unknown_gap":false
}]}}
```

成功返回 `result.finalized=true`，`status` 为 `complete` 或 `partial`，含各流最终统计；handle 随后失效。`partial` 表示成功封闭文件但记录不完整；I/O 失败使用错误 envelope，并尽力保留 `finalized=false/status=failed`，磁盘不可写时清单更新也可能失败。进程 EOF 清理未结束会话，状态尽力标记 `interrupted`，不宣称已完成。HDF5/日志 flush 不代表 fsync 或断电持久化。

终止时仍待处理的未知缺口由 Native 先通过 `capture_event` 保存来源原因，再用最终摘要表达未知边界。边车可能因此额外生成一条 `final_summary` 缺口记录；这些是不同来源的记录，不代表可确定的缺失样本数。

## 只读检查

`{"id":"i","method":"capture_inspect","params":{"path":"<capture目录>"}}`。成功响应的 `result` 含 `finalized/status/verification/streams/issues/recovery_advice`，逐块校验摘要，分批读取，不修改原目录；损坏或未结束可能返回 `verification:"issues"` 或给出 `recovery_advice`。HDF5 断电恢复能力没有保证。

如果清单含 `acquisition_metadata`，检查器按打开时的规则验证后在 `result.acquisition_metadata` 中返回原始内容；验证只检查结构和限制，不认证其来源或真实性。字段无效时返回 `verification:"issues"` 和 `acquisition_metadata_invalid`，不返回无效元数据，但仍尽量检查样本。没有该字段的旧录制保持原有检查结果。

## 错误码与参数

- `protocol.invalid_params`：`{field,reason}`；复用原注册码。
- `capture.session_not_found`：`{handle}`。
- `capture.unavailable`：`{dependency}`；边车依赖不可用时为 `numpy/h5py`，Native 能力协商失败时为 `capture.v1` 或 `capture.metadata.v1`。
- `capture.disk_full`：`{operation,reason}`；operation 可为 `open/append/event/flush/finalize/cleanup`。
- `capture.writer_failed`：`{operation,reason}`；同上；意外异常的 operation 是 `capture_<method>`。

以上捕获错误默认 `retryable=false`，由上层根据状态决定是否新建录制会话。错误码统一登记于 `contracts/error_codes.json`，界面通过 Qt 国际化资源呈现，边车只返回代码及结构化参数。


### 普通文件回放入口

已 finalized 的 capture.v1 可通过普通 `open` / `get_slice` 只读查看；这不改变 capture_* 原始存储/校验协议。选择目录、manifest.json 或配套 signals.h5，按 capture_stream 分别读取各流。返回单位/标定、显式样本时间和原始来源，不强制转换为 µV，也不合并不同流的时钟。文件回放的扩展字段和限制见 ../../../docs/contracts.md“采集目录的只读文件回放”及“共享信号窗口与派生数据”。

只读约束只针对原始 capture 文件，不限制 Native 共享预处理、分析、选区分离与另存导出。原始录制继续直接保存 typed 块；浏览的 float32 投影与派生导出不替代原始存储。
