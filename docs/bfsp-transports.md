---
title: 连接承载规则与设备适配开发规范
doc_type: architecture
status: draft
owner: BoundFree
last_updated: 2026-10-28
---

# 连接承载规则与设备适配开发规范

对应[BFSP线协议](bfsp-protocol.md)。本文件定义 BFSP 的承载 profile 和第三方适配器行为。表中阶段是实施顺序，不表示当前全部已实现。设备原协议的封包由该设备适配器处理，不强制改造成这些底层帧；适配后必须满足统一数据与控制契约。

## 1. 能力矩阵与优先级

| Profile | 首版角色 | 必须实现 | 实施阶段 |
| --- | --- | --- | --- |
| InProcess | 内置模拟器、可安全嵌入 SDK | 规范对象、异步操作、generation 与队列预算 | P1 |
| TCP | 模拟器、自研网关、原生 BFSP 设备 | 流式组帧、握手、限额、心跳、重连 | P2 |
| Serial/USB CDC | 串口设备、虚拟串口设备 | 原生串口参数、校验、重新同步、热插拔 | P2/P4 |
| USB Bulk | 高吞吐自研硬件、私有 USB 协议 | VID/PID/接口选择、传输队列、取消、设备移除 | P4 |
| BLE GATT | 低功耗设备 | 服务识别、MTU、分片、重组、订阅和带宽准入 | P5 |
| TCP+UDP | 需要可控低延迟且允许显式缺口的设备 | 可靠控制配对、报文限额、重排与最终缺口 | P5 |
| AdapterHost | 厂商 DLL、其他语言 SDK、专有适配 | 进程隔离、版本/能力握手、退出与重启隔离 | P4 按需 |

“可靠控制”指需要请求/响应与状态核实的控制语义。UART/BLE 等底层仍可能丢失或断开，不能据此假定命令恰好执行一次。TCP 提供字节流，应用仍须组帧；一次 read 不是一条消息。[RFC 9293](https://www.rfc-editor.org/rfc/rfc9293.html)

## 2. TCP 与本地进程

- 客户端发起连接，不默认扫描全部网段。配置保存 host、port、TLS 模式、可选设备身份约束；地址不等于身份。
- 自研开发网关建议端口 45120，属于本项目建议默认值、非官方注册端口；必须可配置。默认绑定 `127.0.0.1/::1`。
- TCP_NODELAY 用于短控制响应，采样数据按协商块大小批量发送；控制队列优先于下一整帧，不能在一帧中间插入控制帧。
- 连接超时默认 5 秒，HELLO 默认 5 秒，单帧总接收超时 3 秒。限额与心跳仍按协议执行。流控只保证 TCP 字节传输，不保证硬件 FIFO 不溢出。
- 用户取消连接或退出时，关闭 Socket、终止 pending 操作并更新 generation；迟到回调不改变新连接状态。
- 本地 AdapterHost 使用 pipe/stdin/stdout BFSP；stdout 只输出协议，stderr 用结构化诊断。缓冲有界，退出代码和最后心跳时间上报；不得在接收线程无限等待子进程退出。

## 3. 串口与 USB CDC

- EndpointConfig 明确 port/baud/data_bits/parity/stop_bits/flow_control；默认配置由适配器提供，用户选择通用串口时必须能查看和修改。
- 按 USB 序列号＋VID/PID 优先匹配，COM 名称或 `/dev/tty*` 可变化；没有序列号的设备重连要求核实身份，不自动连接任意同型号设备。
- BFSP 原生串口直接使用长度＋CRC 帧，不额外转义。私有串口设备由适配器使用其原有帧头、校验和命令。
- 默认 8N1 的估算有效字节率为 baud/10；其他参数按实际起始、数据、校验、停止位计算。带宽计算包含 BFSP 与控制开销，且留至少 30% 余量。
- 原生 BFSP DATA 每帧在串口上的理论发送时间目标不超过 100 ms，硬准入上限 250 ms，超出则缩短样本块或拒绝配置；单帧接收超时 `max(2 s, 2 × 预计线上时长 + 0.5 s)`，不超过 30 s。
- 控制JSON帧也要通过线上时长检查，最长发送时间不得超过协商idle_timeout的一半，发送器在帧间优先发心跳。描述过大且无法满足该预算时拒绝配置或在握手中协商更长但有上限的idle_timeout，不允许正常长帧触发错误掉线。idle_timeout统一最多30秒，heartbeat_ms最多其三分之一。
- 未经设备声明不能启用 RTS/CTS 或自动发送探测字节；错误探测可能把私有设备切到其他状态。发现列表中的“识别”优先只读身份命令。
- 拔出时结束旧会话，记录 unknown gap，录制清算为中断；插回只自动恢复连接和核实配置，采集是否重启由交互规则决定。

## 4. USB Bulk 与 SDK

- 适配器声明 VID/PID、接口号、端点、传输类型与驱动要求。不能把所有 USB 设备统称为串口。HID、Isochronous、厂商驱动只能由声明支持的 adapter 接入。
- 使用异步、有限数量的 transfer；短传输或 zero-length packet 是底层现象，不直接作为 BFSP 帧尾。传输块内可含半帧或多帧。
- 断开先取消传输，再等待回调排空，最后释放设备句柄；禁止回调访问已销毁 Session。SDK 的内存释放、线程和调用顺序必须来自其文档并有卸载回归。
- Windows 原生 USB 方案、Linux 权限规则和 SDK 分发取决于所选设备。P4 先评估 WinUSB／厂商驱动／跨平台库，不在规划阶段改用户驱动。
- 若设备只提供 SDK 回调，适配器直接构造 SampleBlock；原协议 payload 不必被重新编码成 BFSP。独立进程 SDK 才需要 BFSP 或受约束的共享内存后续扩展。

## 5. BLE GATT 候选 profile

### 5.1 服务与方向

下列自定义 128 位 UUID 为本项目草案值，协议冻结时固定，非 Bluetooth SIG 已分配标准服务。厂商 BLE 设备继续用厂商服务，由适配器转换。

| UUID | 用途 | GATT 属性 |
| --- | --- | --- |
| `22a90e10-112a-4a7c-9e82-73bff9f61300` | BFSP Service | service |
| `22a90e10-112a-4a7c-9e82-73bff9f61301` | Control RX，客户端→设备 | Write with response |
| `22a90e10-112a-4a7c-9e82-73bff9f61302` | Control TX，设备→客户端 | Indicate |
| `22a90e10-112a-4a7c-9e82-73bff9f61303` | Data TX，设备→客户端 | Notify |

先订阅 Control TX 和 Data TX，再发 HELLO。除 DATA 外所有消息走 Control；DATA 走 Data TX。应用须等待操作系统报告订阅成功，不能仅凭 connect 成功就发送 start。配对／加密由 profile 和设备策略要求，失败在 UI 中明确区分于“未发现设备”。

BLE连接超时默认15秒、服务发现/订阅10秒、HELLO总超时15秒；建议HELLO_ACK协商heartbeat_ms=3000、idle_timeout_ms=15000，以覆盖控制帧分片。数据帧短块发送，控制队列保留心跳机会；不能在不调整idle_timeout的情况下允许一个控制帧长期占满链路。

### 5.2 分片包头

每次 characteristic value 为 8 字节分片头＋BFSP 完整帧的一段。ATT notification 的值长度受 ATT_MTU−3 限制，故不能假定一次通知可放完整 BFSP 帧。[Bluetooth ATT 规范](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host/attribute-protocol--att-.html)

| 偏移 | 字段 | 类型 | 含义 |
| --- | --- | --- | --- |
| 0 | version | u8 | 1 |
| 1 | flags | u8 | bit0=FIRST，bit1=LAST，其他0 |
| 2 | message_id | u16le | 每方向、每 characteristic 递增，模 65536 |
| 4 | total_length | u16le | 完整 BFSP 帧长度，包含头与 CRC |
| 6 | offset | u16le | 本片在完整帧中的字节偏移 |
| 8 | fragment | bytes | 长度由当前 GATT value 长度决定 |

有效分片容量为 `ATT_MTU - 3 - 8`，MTU=23 时为 12 字节。不得超过平台实际允许的 characteristic value 大小；允许更短末片。

规则：

1. 默认完整帧上限 4096 字节，可协商提高但绝不超过 65535；BFSP payload 上限相应为 frame_limit−28，JSON 再取协议上限最小值。
2. 每方向、每 characteristic 同时只重组一个帧，禁止交错两条 message_id。FIRST 必须 offset=0；LAST 必须恰好到 total_length；每片至少 1 字节数据。
3. 后续 offset 必须等于已收到长度；重复且字节完全一致的已收分片可忽略；缺片、冲突重复或越界立即丢弃当前重组并计数，直到新 FIRST。
4. 重组缓冲仅在验证总长后分配，完整帧再次执行 BFSP CRC/schema 验证。连接断开清空缓冲，旧 message_id 不跨连接有效。
5. 控制帧重组总超时默认 10 秒，数据帧 2 秒；从 FIRST 开始计时。控制操作的总超时必须涵盖所有分片传送，BLE apply/prepare 可使用协议允许的最高 30 秒。
6. 不做逐数据片应用重传；完整帧丢失由样本序号显示缺口。控制消息仍按请求/响应和状态核实处理，不认为 GATT Write/Indicate ACK 等同于设备已执行命令。
7. message_id 回绕时不得有同 ID 的未结束重组。旧片不能混入新帧，CRC 与样本序号仍要验证。

必须测试 MTU 23、185、247（若平台支持），以及操作系统给出的其他值。声明 BLE 支持只针对实测平台/设备组合，不从一个平台成功推定其他平台成功。

### 5.3 BLE 带宽准入

估算必须计入分片头、通知频率、连接参数和控制保留带宽。未测得持续吞吐前标记“预计”，不得用理论 PHY 速率直接保证采集。默认仅接受预测流量低于实测可用 payload 吞吐 70% 的配置；用户可重新选择通道数、采样率或连接方式。BLE MTU/吞吐降级时不静默降采样，先告警并按能力停止/重新配置。

## 6. TCP+UDP profile

首版 UDP **只传 DATA**，使用 TCP 控制通道完成认证、握手、描述确认、start/stop 和 END。不提供无会话广播控制，也不提供独立 UDP 握手。

- 新增可选命令 `transport.bind_udp`：客户端提供本机接收地址/端口；服务端仅允许控制连接已验证的对端地址，返回数据源地址/端口、随机 128 位 session_token、max_datagram_bytes。重新绑定生成新 token，旧 token 立即失效。
- 单个 UDP datagram 为 `session_token[16] + 完整 BFSP 帧`。默认最大总长 1200 字节，BFSP payload 最大 1156 字节；DATA 样本区最多 1108 字节。不使用 IP 分片或应用层大帧分片；过大的单列通道数据使该配置不支持此 profile。
- token 按原始 16 字节传输，控制响应以 32 个小写 hex 字符表示。先核对来源地址、端口、token、长度，再做 CRC 和 schema。token 绑定会话但不是加密方案；远程非可信网络另需加密承载，首版定位受控局域网。
- 按 stream 单独重排，以 sample_index 为主键；默认等待 50 ms，最多缓存 128 个报文、128 KiB/流且总计不超过 2 MiB/连接，先到任一上限即清算最早缺口并前进。
- 过窗迟到帧丢弃并计数；同一区间字节相同为重复，内容不同或部分重叠为协议错误。已录制时间线不被迟到数据覆盖。
- 正常停止由控制通道 STREAM_END 给出 last_sample_exclusive，最多再等待 100 ms 收齐尾包，然后把缺失区间写入缺口。意外断开则 end unknown。
- 控制连接失效立即使 UDP 会话失效，不因仍有 UDP 到达而保持 Streaming。

若需要无损重传、互联网传输、FEC 或跨网 NAT，应另立 profile 评估，不把当前 UDP 设计包装成通用可靠网络协议。

## 7. 第三方适配器交付契约

每个适配器必须提交以下材料：

| 交付物 | 必填内容 |
| --- | --- |
| adapter manifest | 唯一 ID、API 版本、厂商/型号、支持固件、OS/架构、连接 profile、SDK 依赖 |
| capability mapping | 可配置项目、有效组合、默认值、只读项、开始/停止语义、不可支持项 |
| channel mapping | 原始偏移/编码、通道顺序、单位、标定公式、采样率、参考/带宽来源 |
| time mapping | 硬件计数器宽度、回绕、tick 频率、采样/接收时间区别、重启检测 |
| error mapping | 设备错误→稳定 code、可恢复性、状态核实和重新连接规则 |
| fixtures | 合法/损坏/截断/丢包/回绕的脱敏数据、期望规范块与至少一段真实比对 |
| operations | 驱动、权限、部署、版本检查、取消与卸载顺序 |
| distribution | 原协议/SDK 使用和分发依据、需要用户另行安装的材料 |

配置驱动的简单适配只允许声明固定字节偏移、数值编码、校验、scale/offset 等受约束规则；首版不引入可任意执行脚本的“万能协议编辑器”。复杂状态机、厂商 SDK 用代码适配器。注册表不能凭 VID/PID 自动执行任意下载的二进制。

发现顺序：用户选择来源类别 → 适配器提供候选列表/手动地址 → 只读探测 → 用户选择目标 → 连接/握手 → 描述与实际能力。没有匹配适配器时显示“尚未支持此设备”，提供导出脱敏诊断入口；不盲试一组启动指令。

## 8. 吞吐计算示例（设计输入，不是实测结果）

基础数据率 `R = channels × rate × bytes_per_sample`；每秒 BFSP 开销 `76 × blocks_per_second`；BLE 额外按分片数×8 字节计算，并考虑 GATT 调度开销。

| 场景 | 原始数据率 | 含 BFSP 的示例 | 结论 |
| --- | --- | --- | --- |
| 8 路 × 250 Hz × int24，20 ms/块 | 6000 B/s | 50 × (120+76)=9800 B/s | 115200/8N1 的理论11520 B/s缺少30%余量，拒绝或调整 |
| 32 路 × 1000 Hz × int24，20 ms/块 | 96000 B/s | 50 × (1920+76)=99800 B/s | 不适合115200串口；TCP/USB按实测准入 |
| 256 路 × 30000 Hz × float32，20 ms/块 | 30.72 MB/s | 每帧614476字节，约30.724 MB/s | 作为高负载模拟测试；不承诺BLE或普通串口可承载 |

1 MB=1,000,000 B，1 MiB=1,048,576 B。高负载测试验证架构与内存边界，不表示 Community 扩展了 Pro 的 Spike 采集产品范围。
