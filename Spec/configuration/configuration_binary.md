# Configuration Binary Codec API 与 Schema v1 规范

> 状态：目标契约（normative）
>
> 本文档是 `configuration_binary_decode()`、`configuration_binary_encode()` 以及 Schema v1
> 字节格式的规范来源，可以直接用于派生黑盒测试。实现代码可用于定位缺陷，但不构成测试判据。

## API 契约

### 1. 范围

本文档规定以下两个公开 API 的可观察行为：

- `configuration_binary_decode()`
- `configuration_binary_encode()`

本文档同时规定两个 API 读写的 Schema v1 payload。配置模型的业务有效性由
[Configuration API 规范](configuration.md)中的 `configuration_validate()` 契约定义，本文件只规定
codec 如何使用和映射该校验结果。

本文档不包含 Flash 配置槽的 magic、长度、序列号和 CRC 等存储层元数据，也不规定 codec 的内部辅助
函数、检查顺序或实现算法。

### 2. 公共调用约束

- 调用者必须为每个非空指针提供与参数声明相符的完整、有效对象或内存区域。
- 各输入输出对象的有效内存区域不得重叠。特别是 `payload`、`configuration` 和 `payload_length`
  不得指向同一块或相互重叠的存储空间。
- `decode` 不得修改输入 payload，`encode` 不得修改输入 configuration。
- 当一次调用同时违反多个条件时，本 API 不保证错误检查的优先级。黑盒测试应每次只构造一个独立错误，
  不得依赖内部检查顺序。
- Schema v1 的规范编码是唯一且稳定的。已经由本文档定义的 Schema v1 字节含义不得被后续实现静默
  修改；未来新增 schema 时，兼容或迁移策略另行规定。

### 3. `configuration_binary_decode()`

```c
configuration_binary_codec_result_t configuration_binary_decode(const uint8_t *payload,
                                                                  uint32_t payload_length,
                                                                  configuration_t *configuration);
```

#### 3.1 输入

- `payload` 指向至少 `payload_length` 个可读字节。
- `configuration` 指向一个完整、可写的 `configuration_t` 对象。
- `payload == NULL` 或 `configuration == NULL` 时，函数返回
  `CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT`。
- `payload_length == 0` 或 `payload_length > CONFIGURATION_V1_MAX_PAYLOAD_LENGTH` 时，函数返回
  `CONFIGURATION_BINARY_CODEC_PAYLOAD_LENGTH_INVALID`。
- 非零但小于最短完整 Schema v1 payload 的输入不是长度参数错误；当其 schema 版本可识别但字段不完整
  时，函数返回 `CONFIGURATION_BINARY_CODEC_MALFORMED`。

#### 3.2 成功行为

解码分为以下两个可观察阶段：

1. 按“Schema v1 二进制格式”解析 payload。所有字段必须完整，条件分支值必须受支持，并且解析必须恰好
   消费 `payload_length` 个字节；字段缺失或存在尾随字节均为 malformed。
2. 结构解析成功后，调用 `configuration_validate()` 检查配置模型。

这里的“条件分支值”特指决定后续字段是否存在或采用哪种布局的判别值，包括 `network.mode`、Endpoint
`type`、`mqtt.mode`、`client_id.mode`、MQTT message `mode` 和 collection point `source`。这些判别值
非法时，解码器无法按 Schema v1 唯一确定后续布局，因此返回 `CONFIGURATION_BINARY_CODEC_MALFORMED`。
`schema_version` 例外：不支持的版本返回 `CONFIGURATION_BINARY_CODEC_SCHEMA_UNSUPPORTED`。

固定宽度且不改变后续布局的值在结构阶段按原始字节解码，再由配置模型校验。例如不受支持的
`rtu.baud_rate`、`rtu.frame_format`、寄存器 point `data_type`、MQTT message `qos` 或 `retain`，以及
collection point `qos`，均在结构完整时返回 `CONFIGURATION_BINARY_CODEC_MODEL_INVALID`，而不是
`CONFIGURATION_BINARY_CODEC_MALFORMED`。

只有结构解析和配置模型校验都成功时，函数才返回 `CONFIGURATION_BINARY_CODEC_OK`。成功返回时：

- 本次解码内部执行的 `configuration_validate(configuration)` 已返回
  `CONFIGURATION_VALIDATION_OK`，输出满足配置模型规则。
- 所有已编码字段均按 payload 内容写入 `configuration`。
- 不生效的条件字段、索引大于或等于 `point_count` 的采集点以及定长字节数组中有效内容和结尾 NUL
  之后的元素均为零。
- 每个文本字段在 `bytes[length]` 处包含 NUL `0`；payload 本身不包含该 NUL。
- `configuration_t` 的 C 结构体 padding 值不属于本规范，调用者和测试不得通过整对象 `memcmp`
  比较配置。
- 结果不受 `configuration` 调用前内容影响。

#### 3.3 失败后的输出

函数返回任何非 `CONFIGURATION_BINARY_CODEC_OK` 结果后，`configuration` 的内容未指定，可能保持
原值、被清零或包含部分解码结果。调用者不得读取或使用该对象。函数仍必须保证不会写出完整
`configuration_t` 对象的边界。

#### 3.4 返回结果

| 结果 | 条件 |
|---|---|
| `CONFIGURATION_BINARY_CODEC_OK` | Schema v1 结构完整、恰好消费整个 payload，且模型校验成功 |
| `CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT` | `payload` 或 `configuration` 为空 |
| `CONFIGURATION_BINARY_CODEC_PAYLOAD_LENGTH_INVALID` | `payload_length` 为 0 或大于 7826 |
| `CONFIGURATION_BINARY_CODEC_SCHEMA_UNSUPPORTED` | payload 的 schema 版本不是 `0x01` |
| `CONFIGURATION_BINARY_CODEC_MALFORMED` | 字段缺失、条件分支值非法、文本长度越界、采集点过多或存在尾随字节 |
| `CONFIGURATION_BINARY_CODEC_MODEL_INVALID` | 结构解析成功，但模型校验返回除资源不足以外的非成功结果 |
| `CONFIGURATION_BINARY_CODEC_RESOURCE_UNAVAILABLE` | 模型校验返回 `CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE` |

`configuration_binary_decode()` 不返回 `CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL`。

#### 3.5 副作用与执行上下文

- 函数不访问 Flash、网络或 RTC，也不改变持久配置或设备当前运行配置。
- 模型校验可以临时分配并释放内存、获取并释放锁，以及在资源分配失败时输出调试日志。
- 所有临时资源必须在函数返回前释放。
- 函数只能在系统 heap 和相关分配器初始化完成后的普通任务上下文调用，不支持 ISR。
- 调用者必须串行化不同的 `configuration_binary_decode()` 调用；本 API 不保证并发调用安全。

### 4. `configuration_binary_encode()`

```c
configuration_binary_codec_result_t configuration_binary_encode(const configuration_t *configuration,
                                                                  uint8_t *payload,
                                                                  uint32_t payload_capacity,
                                                                  uint32_t *payload_length);
```

#### 4.1 前置条件与输入

- `configuration` 必须是已经通过模型校验的配置，即调用者已经确认
  `configuration_validate(configuration) == CONFIGURATION_VALIDATION_OK`。
- 对不满足上述模型前置条件的对象，本 API 不规定返回结果、payload 内容或内存安全行为；黑盒测试不得
  使用此类输入推导契约。
- `payload` 指向至少 `payload_capacity` 个可写字节。
- `payload_length` 指向一个可写的 `uint32_t` 对象。
- `configuration`、`payload` 或 `payload_length` 任一为空时，函数返回
  `CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT`。
- 本 API 不支持长度查询；即使 `payload_capacity == 0`，`payload == NULL` 仍是无效参数。

#### 4.2 成功行为

函数按“Schema v1 二进制格式”生成唯一的规范编码。成功返回时：

- 结果为 `CONFIGURATION_BINARY_CODEC_OK`。
- `*payload_length` 是实际编码长度，范围为 21 至
  `CONFIGURATION_V1_MAX_PAYLOAD_LENGTH`。
- `payload[0]` 至 `payload[*payload_length - 1]` 包含完整 payload。
- `payload_capacity` 恰好等于所需长度时也必须成功；容量可以大于
  `CONFIGURATION_V1_MAX_PAYLOAD_LENGTH`。
- 不生效的条件字段和索引大于或等于 `point_count` 的采集点不参与编码，其内容不影响输出。
- 对相同的生效字段值，函数必须生成完全相同的 payload。
- 当 `*payload_length < payload_capacity` 时，`payload[*payload_length]` 至
  `payload[payload_capacity - 1]` 的内容未指定，调用者和测试不得依赖。

#### 4.3 失败后的输出

函数返回任何非 `CONFIGURATION_BINARY_CODEC_OK` 结果时，即使 `payload_length != NULL`，
`*payload_length` 的值也未定义：它可以保持调用前的值，也可以被修改。调用者不得读取或使用该值，
黑盒测试也不得对其作任何断言。

当 `payload_capacity` 小于所需长度时，函数返回
`CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL`。此时 payload 可以保持原值或包含部分编码前缀，其内容
未指定，调用者不得使用。函数仍必须保证不会写出 `payload_capacity` 指定的区域。

#### 4.4 返回结果

对满足模型前置条件的调用，函数只返回以下结果：

| 结果 | 条件 |
|---|---|
| `CONFIGURATION_BINARY_CODEC_OK` | payload 编码完整 |
| `CONFIGURATION_BINARY_CODEC_INVALID_ARGUMENT` | `configuration`、`payload` 或 `payload_length` 为空 |
| `CONFIGURATION_BINARY_CODEC_BUFFER_TOO_SMALL` | `payload_capacity` 小于完整编码所需长度 |

共享结果枚举中的其他值不是满足模型前置条件时的 `encode` 结果。

#### 4.5 副作用与并发

- 函数除写入 `payload` 和 `payload_length` 外，不分配内存、不获取锁、不输出日志，也不访问 Flash、
  网络、RTC 或其他硬件。
- 在每次调用使用独立且不重叠的输入输出对象，并且输入 configuration 在调用期间不被修改时，函数可
  重入且可并发调用。
- 本规范不承诺中断执行时间上限，也不将可重入保证扩展为 ISR 实时适用性保证。

## Schema v1 二进制格式

本节只定义 codec payload，不包含 API 的参数检查、失败后输出状态或执行上下文。

### 1. 总体约定

- payload 是紧凑的顺序编码，不是 `configuration_t` 的内存镜像；结构体 padding 不会写入。
- 第一个字节固定为 schema 版本。当前唯一支持的版本是 `0x01`。
- `u16` 和 `u32` 均按小端字节序编码。
- 字段之间没有对齐字节或 padding。
- payload 自身不包含总长度、结束标记、magic 或 CRC；总长度由 API 的 `payload_length` 参数提供。
- IPv4 固定编码为 4 个八位组，顺序与点分十进制一致。例如 `192.168.1.10` 编码为
  `C0 A8 01 0A`，不把地址当作小端 `u32` 处理。
- 所有文本均以字节为单位计长，格式为 `u16 length` 后紧跟 `length` 个内容字节。内存中的结尾 NUL
  不写入 payload，解码器会在内容之后补一个 NUL。
- 条件字段在条件不成立时完全省略，不编码零值占位。
- 解码必须恰好消费整个 payload；任何缺失字段或尾随字节都会使 payload 被判定为 malformed。

#### 1.1 基本类型

| 记法 | 字节数 | 编码 |
|---|---:|---|
| `u8` | 1 | 无符号 8 位整数 |
| `u16` | 2 | 无符号 16 位整数，小端 |
| `u32` | 4 | 无符号 32 位整数，小端 |
| `ipv4` | 4 | `octet[0]`、`octet[1]`、`octet[2]`、`octet[3]` |
| `text(N)` | `2 + N` | `u16 N`，随后为 `N` 个原始内容字节；不包含结尾 NUL |

### 2. 顶层布局

除第一个字节外，后续字段的绝对偏移会受条件字段和文本长度影响，因此下表使用编码顺序而非固定偏移。

| 顺序 | 字段或块 | 编码 | 出现条件 |
|---:|---|---|---|
| 1 | `schema_version` | `u8`，固定为 `0x01` | 始终 |
| 2 | `network` | [Network 块](#3-network-块) | 始终 |
| 3 | `rtu` | [RTU 块](#4-rtu-块) | 始终 |
| 4 | `modbus_tcp` | [Modbus TCP 块](#5-modbus-tcp-块) | 始终 |
| 5 | `sntp.servers[0]` | [Endpoint 块](#6-endpoint-块) | 始终 |
| 6 | `sntp.servers[1]` | [Endpoint 块](#6-endpoint-块) | 始终 |
| 7 | `mqtt` | [MQTT 块](#7-mqtt-块) | 始终 |
| 8 | `collection` | [Collection 块](#8-collection-块) | 始终 |

等价的结构表示如下：

```text
payload_v1 =
    u8                 schema_version
    network_block      network
    rtu_block          rtu
    modbus_tcp_block   modbus_tcp
    endpoint_block     sntp_server_0
    endpoint_block     sntp_server_1
    mqtt_block         mqtt
    collection_block   collection
```

### 3. Network 块

| 顺序 | 字段 | 编码 | 取值或长度 | 出现条件 |
|---:|---|---|---|---|
| 1 | `network.mode` | `u8` | `0` = DHCP，`1` = STATIC | 始终 |
| 2 | `network.ip_address` | `ipv4` | 4 字节 | `mode == 1` |
| 3 | `network.subnet_mask` | `ipv4` | 4 字节 | `mode == 1` |
| 4 | `network.gateway` | `ipv4` | 4 字节 | `mode == 1` |
| 5 | `network.dns_primary` | `ipv4` | 4 字节 | `mode == 1` |
| 6 | `network.dns_secondary` | `ipv4` | 4 字节 | `mode == 1` |

DHCP 块长度为 1 字节，STATIC 块长度为 21 字节。其他 `mode` 值不是合法编码。

### 4. RTU 块

| 顺序 | 字段 | 编码 | 字节数 |
|---:|---|---|---:|
| 1 | `rtu.baud_rate` | `u32` | 4 |
| 2 | `rtu.frame_format` | `u8` | 1 |
| 3 | `rtu.first_byte_timeout_ms` | `u16` | 2 |

`frame_format` 的编码值如下：

| 值 | 格式 |
|---:|---|
| `0` | 8N1 |
| `1` | 8E1 |
| `2` | 8O1 |
| `3` | 8N2 |

RTU 块固定为 7 字节。

### 5. Modbus TCP 块

| 顺序 | 字段 | 编码 | 字节数 |
|---:|---|---|---:|
| 1 | `modbus_tcp.listen_port` | `u16` | 2 |

### 6. Endpoint 块

两个 SNTP 服务器分别编码为一个 Endpoint 块。

| 顺序 | 字段 | 编码 | 取值或长度 | 出现条件 |
|---:|---|---|---|---|
| 1 | `type` | `u8` | `0` = HOSTNAME，`1` = IPV4 | 始终 |
| 2 | `hostname` | `text(N)` | `1 <= N <= 253` | `type == 0` |
| 2 | `ipv4` | `ipv4` | 4 字节 | `type == 1` |

HOSTNAME Endpoint 长度为 `3 + N` 字节，IPV4 Endpoint 固定为 5 字节。其他 `type` 值不是合法编码。

### 7. MQTT 块

| 顺序 | 字段或块 | 编码 | 取值或长度 | 出现条件 |
|---:|---|---|---|---|
| 1 | `mqtt.mode` | `u8` | `0` = DISABLED，`1` = ENABLED | 始终 |
| 2 | `mqtt.broker_address` | `text(N)` | `1 <= N <= 253` | `mode == 1` |
| 3 | `mqtt.broker_port` | `u16` | 2 字节 | `mode == 1` |
| 4 | `mqtt.client_id` | [Client ID 块](#71-client-id-块) | 可变 | `mode == 1` |
| 5 | `mqtt.username` | `text(N)` | `1 <= N <= 32` | `mode == 1` |
| 6 | `mqtt.password` | `text(N)` | `1 <= N <= 64` | `mode == 1` |
| 7 | `mqtt.ca_certificate_pem` | `text(N)` | `1 <= N <= 4096` | `mode == 1` |
| 8 | `mqtt.keep_alive_seconds` | `u16` | 2 字节 | `mode == 1` |
| 9 | `mqtt.online_message` | [MQTT 消息块](#72-mqtt-消息块) | 可变 | `mode == 1` |
| 10 | `mqtt.will_message` | [MQTT 消息块](#72-mqtt-消息块) | 可变 | `mode == 1` |

DISABLED MQTT 块只包含 `mqtt.mode`，长度为 1 字节。其他 `mode` 值不是合法编码。

#### 7.1 Client ID 块

| 顺序 | 字段 | 编码 | 取值或长度 | 出现条件 |
|---:|---|---|---|---|
| 1 | `client_id.mode` | `u8` | `0` = DERIVED，`1` = EXPLICIT | 始终 |
| 2 | `client_id.explicit_value` | `text(N)` | `1 <= N <= 23` | `mode == 1` |

DERIVED 块长度为 1 字节；EXPLICIT 块长度为 `3 + N` 字节。其他 `mode` 值不是合法编码。

#### 7.2 MQTT 消息块

上线消息和遗嘱消息使用相同的编码。

| 顺序 | 字段 | 编码 | 取值或长度 | 出现条件 |
|---:|---|---|---|---|
| 1 | `message.mode` | `u8` | `0` = DISABLED，`1` = CUSTOM | 始终 |
| 2 | `message.topic` | `text(N)` | `1 <= N <= 128` | `mode == 1` |
| 3 | `message.payload` | `text(N)` | `0 <= N <= 128` | `mode == 1` |
| 4 | `message.qos` | `u8` | 1 字节 | `mode == 1` |
| 5 | `message.retain` | `u8` | 1 字节 | `mode == 1` |

DISABLED MQTT 消息块长度为 1 字节；CUSTOM 块长度为
`7 + topic_length + payload_length` 字节。其他 `mode` 值不是合法编码。

### 8. Collection 块

| 顺序 | 字段或块 | 编码 | 取值或长度 | 出现条件 |
|---:|---|---|---|---|
| 1 | `collection.point_count` | `u8` | `0 <= point_count <= 16` | 始终 |
| 2 | `collection.points[i]` | [Collection Point 块](#81-collection-point-块) | 可变 | 重复 `point_count` 次 |

payload 中只编码索引 `0` 至 `point_count - 1` 的采集点。固定容量数组中其余采集点不写入。

#### 8.1 Collection Point 块

| 顺序 | 字段 | 编码 | 取值或长度 | 出现条件 |
|---:|---|---|---|---|
| 1 | `point.slave_address` | `u8` | 1 字节 | 始终 |
| 2 | `point.source` | `u8` | 见下表 | 始终 |
| 3 | `point.address` | `u16` | 2 字节 | 始终 |
| 4 | `point.data_type` | `u8` | `0` = UINT16，`1` = INT16 | `source == 2` 或 `source == 3` |
| 5 | `point.poll_interval_ms` | `u32` | 4 字节 | 始终 |
| 6 | `point.first_byte_timeout_ms` | `u16` | 2 字节 | 始终 |
| 7 | `point.topic` | `text(N)` | `1 <= N <= 128` | 始终 |
| 8 | `point.qos` | `u8` | 1 字节 | 始终 |

`source` 的编码值如下：

| 值 | 数据源 | 是否编码 `data_type` |
|---:|---|---|
| `0` | COIL | 否 |
| `1` | DISCRETE_INPUT | 否 |
| `2` | HOLDING_REGISTER | 是 |
| `3` | INPUT_REGISTER | 是 |

COIL 或 DISCRETE_INPUT Point 块长度为 `13 + topic_length` 字节；HOLDING_REGISTER 或
INPUT_REGISTER Point 块长度为 `14 + topic_length` 字节。其他 `source` 值不是合法编码。

### 9. 长度边界

Schema v1 payload 最大长度为 `7826` 字节，且该长度包含开头的 schema 版本字节。最大长度由所有可变项
取最大分支和最大长度得到：

| 部分 | 最大字节数 | 计算方式 |
|---|---:|---|
| Schema 版本 | 1 | `u8` |
| Network | 21 | STATIC 模式和 5 个 IPv4 地址 |
| RTU | 7 | `u32 + u8 + u16` |
| Modbus TCP | 2 | `u16` |
| SNTP | 512 | 两个最大 hostname Endpoint：`2 * (1 + 2 + 253)` |
| MQTT | 5010 | ENABLED、最大文本、显式 Client ID、两条最大 CUSTOM 消息 |
| Collection | 2273 | `1 + 16 * 142`，16 个最大寄存器采集点 |
| **合计** | **7826** | `CONFIGURATION_V1_MAX_PAYLOAD_LENGTH` |

最短的结构完整 payload 为 21 字节：使用 DHCP、两个长度为 1 的 HOSTNAME Endpoint、禁用 MQTT，且
采集点数量为 0。结构完整不代表字段值一定通过配置模型校验。

默认配置编码长度为 `48` 字节：

```text
1                                      schema_version
+ 1                                    DHCP Network
+ 7                                    RTU
+ 2                                    Modbus TCP
+ (1 + 2 + 14)                         "ntp.aliyun.com" Endpoint
+ (1 + 2 + 15)                         "ntp.tencent.com" Endpoint
+ 1                                    DISABLED MQTT
+ 1                                    point_count = 0
= 48
```
