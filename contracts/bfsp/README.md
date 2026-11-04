# BFSP 1.0 候选契约

本目录保存候选协议注册表、JSON Schema 和人工构造的跨语言线格式向量。协议尚未冻结；向量不含真实采集数据。协议字段与语义见仓库内的 [BFSP 协议规范](../../docs/bfsp-protocol.md)，承载方式见 [传输规范](../../docs/bfsp-transports.md)。

- `registry.json` 登记消息类型、字节长度、dtype、单位、能力、候选传输和本地错误码。列出传输方式不代表已实现或验收。
- `messages.schema.json` 的 `$defs` 按注册表 `schema_ref` 选择对应 JSON 消息；DATA 是二进制，不用此 Schema。Schema 的未知可选字段可以扩展。
- `frame-vectors.json` 和 `stream-descriptor.json` 是独立的合成参考样例。帧直接以完整十六进制保存，C++ 编解码测试逐字节比对。

Schema 不能单独表达 UTF-8 字节长度、重复键、总嵌套深度、`stream.accept` 按 stream_id 去重、`data.gap` 的 u64 加法溢出、同流描述版本、协商预算、样本长度、设备状态等上下文约束。C++ 协议校验器负责去重和区间溢出；会话层必须确认接受列表完整、身份及状态正确。独立消息校验限制每个 `receive_limits` 值的范围；`max_json <= max_payload` 等关联关系、与本端限额的协商、请求与响应的关联以及必需 feature 由会话层校验。数字 u64/i64 使用规范十进制字符串，禁止前导零，以免 JSON number 损失精度。

原生测试目标为 `bfsp_protocol_test`，任选以下方式指定向量位置：

```bash
bfsp_protocol_test /path/to/boundfree-community
bfsp_protocol_test /path/to/boundfree-community/contracts/bfsp/frame-vectors.json
BF_REPO_ROOT=/path/to/boundfree-community bfsp_protocol_test
```

如果构建时定义了 `BF_REPO_ROOT` 宏，未传参数且未设置同名环境变量时使用该宏指定的仓库路径。

Python 契约测试：

```bash
python3 -m pytest -q tests/contracts/test_bfsp_contracts.py
```

`pytest` 和 `jsonschema` 仅用于测试，运行时不依赖它们。若环境缺少，可在测试环境中安装：`python3 -m pip install pytest jsonschema`。缺少 `jsonschema` 时 Schema 专项测试会明确标记 skipped，其余标准库测试仍运行。
