---
title: 基础契约
doc_type: process
status: active
owner: BoundFree
last_updated: 2026-07-08
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

控制消息使用 JSON。M1-A 先实现 `hello`，后续阶段扩展 `open/get_slice/close`。

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

规则：

- `id` 由主程序生成，响应必须原样返回。
- `method` 是稳定协议名，不参与本地化。
- `params` 只放结构化数据，不放用户可见完整句子。
- `ok=false` 时必须包含 `error.code`。
- `trace_id` 用于日志和售后定位，UI 默认不把它当正文文案。
- 后续二进制 payload 统一小端，EEG 数据形状为 `[n_channels, n_samples]`，单位为 `uV`。

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

## i18n Contract

i18n 使用 Qt Linguist `.ts/.qm`。v1 stable 必达语言为：

- `zh_CN`
- `en_US`

开发和静态检查保留：

- `pseudo`

以下 locale 文件可继续保留，用于架构扩展和 key 完整性检查，但不作为 v1 stable 验收项：

- `zh_TW`
- `ja_JP`
- `ko_KR`
- `de_DE`
- `fr_FR`
- `es_ES`

M1-A 实际验证 `zh_CN`、`en_US`、`pseudo`，其他目标语言保持文件结构和 key 完整性。

规则：

- 默认语言为 `zh_CN`。
- 启动时优先读取用户设置；没有设置时匹配系统语言；无法匹配则回退 `zh_CN`。
- 常驻控件必须支持 `retranslateUi()`。
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
