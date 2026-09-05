# Management Frame 格式摘要

> 本文是本仓库 Management Transport 线路协议、固件实现与主机测试的规范依据。
> Configuration Payload 的格式见
> [Configuration Binary Codec API 与 Schema v1 规范](../configuration/configuration_binary.md)。

## 1. 帧结构

Management Frame 是承载在 USB CDC 字节流上的二进制帧。所有多字节整数均使用 little-endian。

```text
+----------+--------------+----------------+----------------+-------------+---------+
| magic    | message_type | transaction_id | payload_length | payload     | crc32   |
| 4 bytes  | 1 byte       | 4 bytes LE     | 4 bytes LE     | N bytes     | 4 B LE  |
+----------+--------------+----------------+----------------+-------------+---------+
  offset 0   offset 4       offset 5         offset 9         offset 13     13 + N
```

| 偏移 | 字段 | 类型 | 说明 |
|---:|---|---|---|
| 0 | `magic` | 4 bytes | 固定 ASCII `MBGW`，十六进制为 `4D 42 47 57` |
| 4 | `message_type` | `u8` | 请求或响应类型 |
| 5 | `transaction_id` | `u32 LE` | 由主机生成；响应沿用请求 ID |
| 9 | `payload_length` | `u32 LE` | payload 字节数，范围为 0～8192 |
| 13 | `payload` | N bytes | 具体格式由 `message_type` 决定 |
| 13 + N | `crc32` | `u32 LE` | 覆盖从 `magic` 到 payload 末尾的全部字节 |

- 固定头长度：13 字节。
- 帧开销：17 字节，即固定头 13 字节加 CRC 4 字节。
- 总帧长度：`17 + payload_length`。
- 最大 payload：8192 字节。
- 最大总帧长度：8209 字节。
- 帧中没有 protocol version、flags、header CRC、分块序号或 USB packet ACK。

## 2. CRC32

CRC 使用 reflected CRC-32/ISO-HDLC，与 Ethernet 和 zlib CRC32 相同：

| 参数 | 值 |
|---|---:|
| Reflected polynomial | `0xEDB88320` |
| Initial value | `0xFFFFFFFF` |
| Final XOR | `0xFFFFFFFF` |
| 校验向量 `123456789` | `0xCBF43926` |

计算范围不包含 CRC 字段本身。计算出的 `u32` 在线路中按 little-endian 写入。

## 3. 消息类型

| 值 | 名称 | 方向 | 请求 payload | 成功响应 payload |
|---:|---|---|---|---|
| `0x01` | `GET_ACTIVE_CONFIGURATION` | Host → Device | 空 | — |
| `0x81` | `GET_ACTIVE_CONFIGURATION_RESPONSE` | Device → Host | — | `result_code + Configuration Payload` |
| `0x02` | `PUT_CONFIGURATION` | Host → Device | Configuration Payload | — |
| `0x82` | `PUT_CONFIGURATION_RESPONSE` | Device → Host | — | `result_code` |
| `0x03` | `GET_STATUS` | Host → Device | 空 | — |
| `0x83` | `GET_STATUS_RESPONSE` | Device → Host | — | `result_code + Device Status` |
| `0x04` | `RESTART` | Host → Device | 空 | — |
| `0x84` | `RESTART_RESPONSE` | Device → Host | — | `result_code` |
| `0xFF` | `ERROR_RESPONSE` | Device → Host | — | `result_code` |

所有响应 payload 都以两字节 `u16 LE result_code` 开始：

```text
成功且有数据：  result_code(2) | response_data(...)
成功且无数据：  result_code(2)
失败响应：      result_code(2)
```

未知 message type 或把响应类型发给设备时，设备返回
`ERROR_RESPONSE(UNSUPPORTED_MESSAGE)`。要求空 payload 的命令携带了数据时，设备返回该命令对应的响应类型和
`INVALID_REQUEST`。

## 4. 结果码

| 值 | 名称 | 含义 |
|---:|---|---|
| `0` | `OK` | 操作成功 |
| `1` | `INVALID_REQUEST` | 已识别命令的 payload 形状无效 |
| `2` | `UNSUPPORTED_MESSAGE` | 不支持该 message type |
| `3` | `CONFIGURATION_INVALID` | Configuration Payload 校验失败 |
| `4` | `RESOURCE_UNAVAILABLE` | 校验所需临时资源不可用 |
| `5` | `STORAGE_IO_ERROR` | External Flash 写入或回读校验失败 |
| `6` | `NOT_READY` | Configuration Service 本次启动不可用 |
| `7` | `INTERNAL_ERROR` | 固件内部不变量、编码或固定容量错误 |

协议没有 `BUSY` 结果码。

## 5. 各命令的 payload

### 5.1 GET_ACTIVE_CONFIGURATION

请求 payload 为空。

成功响应：

```text
0x81 payload = result_code:u16 LE | Configuration Payload
```

Configuration Payload 是本次启动正在使用的 Active Configuration。失败响应仅包含 `result_code`。

### 5.2 PUT_CONFIGURATION

请求：

```text
0x02 payload = Configuration Payload
```

响应：

```text
0x82 payload = result_code:u16 LE
```

`OK` 表示配置已经持久化并通过回读校验，但当前运行中的 Active Configuration 不会改变，也不会自动重启。

### 5.3 GET_STATUS

请求 payload 为空。成功响应 payload 长度固定为 17 字节：两字节 `OK` 加 15 字节 Device Status。

| 响应 payload 偏移 | Device Status 偏移 | 字段 | 类型 | 说明 |
|---:|---:|---|---|---|
| 0 | — | `result_code` | `u16 LE` | 成功时为 0 |
| 2 | 0 | `ethernet_link` | `u8` | 0 = down，1 = up |
| 3 | 1 | `ipv4` | 4 bytes | 按点分顺序保存四个 octet |
| 7 | 5 | `sntp_synchronized` | `u8` | 0 = 未同步，1 = 已同步 |
| 8 | 6 | `unix_seconds` | `u32 LE` | RTC Unix 秒 |
| 12 | 10 | `microseconds` | `u32 LE` | 当前秒内微秒 |
| 16 | 14 | `mqtt_state` | `u8` | MQTT Publisher 状态 |

SNTP 未同步时，不读取 RTC，`unix_seconds` 和 `microseconds` 均为 0；已同步但读取时间失败时，这两个字段也为 0。

`mqtt_state` 取值：

| 值 | 状态 |
|---:|---|
| 0 | `DISABLED` |
| 1 | `DISCONNECTED` |
| 2 | `CONNECTING` |
| 3 | `CONNECTED` |
| 4 | `ERROR` |

### 5.4 RESTART

请求 payload 为空，响应 payload 只有两字节 result code：

```text
0x84 payload = result_code:u16 LE
```

设备必须先完成 `RESTART_RESPONSE(OK)` 的异步发送，再在 task context 中重启。session 关闭、USB reset 或
disconnect 会取消尚未执行的重启。

## 6. Transaction 与 Session

- 主机在一个 Management Session 内同时只能有一个未完成请求。
- `transaction_id` 是任意 `u32`，响应必须沿用请求 ID。
- 当前请求未完成时收到不同 ID，设备静默丢弃，不排队，也不返回 `BUSY`。
- 一个 transaction 完成后，可以发起下一请求。
- session close/open 会清除 parser、当前 transaction 和待重启状态。
- `CDC_Init` 打开 session；`CDC_DeInit`、USB reset 或 disconnect 关闭 session；DTR 不参与 session 语义。

## 7. 字节流解析规则

- USB CDC packet、callback 和 2048 字节 CDC buffer 都不是帧边界。
- 一个 Management Frame 可以跨任意多个 USB packet；一个 packet 也可以包含多个连续帧。
- parser 使用滚动 `MBGW` 匹配，允许 magic 跨 callback，并跳过 magic 前的噪声。
- magic 错误、payload 超过 8192 字节、CRC 错误或帧未完成时，不执行命令，也不发送线路错误响应。
- 超长声明或坏 CRC 后，parser 继续搜索后续合法 `MBGW` 帧。
- session 结束时丢弃尚未完成的半帧。

## 8. 完整请求示例

以下是 `GET_STATUS` 请求，`transaction_id = 0x12345678`，payload 为空：

```text
4D 42 47 57  03  78 56 34 12  00 00 00 00  51 E4 C0 0E
|-- MBGW --| type |---- tx id ----| |-- length --| |--- CRC32 ---|
```

- `message_type = 0x03`
- `transaction_id` 在线路中编码为 `78 56 34 12`
- `payload_length = 0`
- CRC 数值为 `0x0EC0E451`，在线路中编码为 `51 E4 C0 0E`
- 总帧长度为 17 字节
