# Configuration API 规范

> 状态：已批准，可用于派生黑盒测试
>
> 本文档已经审核通过，是 Configuration API 可观察行为以及黑盒测试预期结果的唯一规范来源。
>
> 编写本规范时曾参考现有实现，以发现已有行为、边界和遗漏；实现本身不构成测试依据，
> 也不能覆盖经审核确认的规范。

## 范围

本文档仅规定以下三个公开 API：

- `configuration_set_defaults()`
- `configuration_validate()`
- `configuration_equals()`

本文档只描述 API 调用者能够观察到的行为，不规定内部辅助函数、校验顺序或实现算法。

## `configuration_set_defaults()`

### 输入

`configuration` 可以为 `NULL`。当 `configuration == NULL` 时，函数直接返回且不产生副作用。

### 默认值

当 `configuration != NULL` 时，函数将配置设为以下默认值：

- 网络模式为 DHCP，所有静态 IPv4 字段为零。
- Modbus RTU 波特率为 `9600 bit/s`，帧格式为 `8N2`，首字节超时为 `1000 ms`。
- Modbus TCP 监听端口为 `502`。
- 首选 SNTP 服务器为域名 `ntp.aliyun.com`。
- 备用 SNTP 服务器为域名 `ntp.tencent.com`。
- MQTT 为禁用状态。
- 采集点数量为 `0`。

除上述非零默认值外，函数将所有公开字段清零，包括：

- DHCP 模式下未生效的静态网络字段；
- MQTT broker、客户端标识、用户名、密码、CA 证书、上线消息和遗嘱消息字段；
- 所有未使用的采集点；
- 所有定长字节数组中位于有效内容以外的元素。

本规范不规定 C 结构体填充字节（padding）的值。

### 保证

- 函数生成的默认配置必须使 `configuration_validate()` 返回
  `CONFIGURATION_VALIDATION_OK`。这项保证不要求本函数内部调用 `configuration_validate()`。
- 函数的结果不受目标对象原有内容影响。
- 对同一对象连续调用本函数两次，所得结果与只调用一次相同。
- 除写入 `configuration` 指向的对象外，函数不产生其他副作用。

## `configuration_validate()`

### 输入与安全性

- 当 `configuration == NULL` 时，函数返回 `CONFIGURATION_VALIDATION_INVALID_ARGUMENT`。
- 当 `configuration != NULL` 且指向一个完整的 `configuration_t` 对象时，无论对象中各字段是否合法，
  函数都必须安全返回一个 `configuration_validation_result_t` 结果，不得越界访问或崩溃。
- 函数不得修改传入的配置对象。

### 不生效字段

下列字段不参与校验，其内容不影响校验结果：

- DHCP 模式下的所有静态网络字段；
- MQTT 禁用时除 `mqtt.mode` 外的所有 MQTT 字段；
- 对应 MQTT 消息禁用时，该消息的 topic、payload、QoS 和 retain；
- 派生 client ID 模式下的显式 client ID；
- 采集点数组中索引大于或等于 `point_count` 的元素；
- Coil 和 Discrete Input 采集点的 `data_type`。

### 多个错误

当一份配置同时违反多项规则时，本 API 不保证返回错误的先后顺序。调用者不得依赖某一种错误必定优先
于另一种错误；黑盒测试也不得依赖内部校验顺序。

### 可变长度字节字段

对所有生效的 hostname、client ID、用户名、密码、证书、topic 和 payload：

- `length` 表示内容的字节数，而不是 Unicode 字符数。
- 当 `length > 0` 时，有效内容位于 `bytes[0]` 至 `bytes[length - 1]`，并且
  `bytes[length]` 必须为 NUL `0`。
- 位于有效内容和 NUL 哨兵之后的数组元素不参与校验。
- 文本的有效内容不得包含内嵌 NUL。

### 返回值

函数只返回以下结果：

- `CONFIGURATION_VALIDATION_OK`：所有生效字段均有效。
- `CONFIGURATION_VALIDATION_INVALID_ARGUMENT`：`configuration == NULL`。
- `CONFIGURATION_VALIDATION_NETWORK_INVALID`：网络配置无效。
- `CONFIGURATION_VALIDATION_RTU_INVALID`：Modbus RTU 配置无效。
- `CONFIGURATION_VALIDATION_MODBUS_TCP_INVALID`：Modbus TCP 配置无效。
- `CONFIGURATION_VALIDATION_SNTP_INVALID`：SNTP 配置无效。
- `CONFIGURATION_VALIDATION_MQTT_INVALID`：MQTT 普通字段无效。
- `CONFIGURATION_VALIDATION_CERTIFICATE_INVALID`：MQTT CA 证书无效。
- `CONFIGURATION_VALIDATION_COLLECTION_INVALID`：采集配置无效。
- `CONFIGURATION_VALIDATION_BUS_UTILIZATION_EXCEEDED`：预计 Modbus RTU 总线利用率超过限制。
- `CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE`：校验 MQTT CA 证书期间所需的内存资源不足。

### 副作用边界

函数只检查配置值，并可在内存中临时解析证书。函数不得修改传入配置、查询 DNS、连接 MQTT 或 SNTP
服务器、访问硬件，或者改变设备当前运行配置。

### Network

`network.mode` 只允许以下值：

- `CONFIGURATION_NETWORK_MODE_DHCP`
- `CONFIGURATION_NETWORK_MODE_STATIC`

其他值使函数返回 `CONFIGURATION_VALIDATION_NETWORK_INVALID`。DHCP 模式下的静态网络字段不参与校验。

STATIC 模式必须同时满足以下规则：

- `subnet_mask` 必须是由连续的 `1` 后跟连续的 `0` 组成的 `/1～/30` IPv4 子网掩码；
  `/0`、`/31` 和 `/32` 均无效。
- `ip_address` 和 `gateway` 均不得为 `0.0.0.0`，首字节必须在 `1～223` 范围内，并且不得属于
  `127.0.0.0/8` 回环地址段或 `169.254.0.0/16` 链路本地地址段。
- `ip_address` 和 `gateway` 的 host 部分均不得全为 `0` 或全为 `1`。
- `ip_address` 和 `gateway` 必须位于同一子网，并且二者不得相同。
- `dns_primary` 和 `dns_secondary` 均为必填项。二者均不得为 `0.0.0.0`，首字节必须在
  `1～223` 范围内，并且不得属于 `127.0.0.0/8` 或 `169.254.0.0/16`。
- `dns_primary` 和 `dns_secondary` 不得相同，但无须与 `ip_address` 位于同一子网。

违反以上任意一项 Network 规则时，函数返回 `CONFIGURATION_VALIDATION_NETWORK_INVALID`。

### Modbus RTU

`rtu.baud_rate` 只允许以下值，单位为 `bit/s`：

- `1200`
- `2400`
- `4800`
- `9600`
- `19200`
- `38400`
- `57600`
- `115200`

`rtu.frame_format` 只允许 `8N1`、`8E1`、`8O1` 和 `8N2`。`rtu.first_byte_timeout_ms`
必须位于 `50～3000 ms` 闭区间内。

违反以上任意一项 Modbus RTU 规则时，函数返回 `CONFIGURATION_VALIDATION_RTU_INVALID`。

### Modbus TCP

`modbus_tcp.listen_port` 必须位于 `1～65535` 闭区间内。校验不检查端口是否已被占用，也不检查其
是否与其他本地服务冲突。

端口为 `0` 时，函数返回 `CONFIGURATION_VALIDATION_MODBUS_TCP_INVALID`。

### SNTP

`sntp.servers` 中的首选和备用服务器均为必填项。每个服务器的 `type` 只允许以下值：

- `CONFIGURATION_ENDPOINT_ADDRESS_TYPE_HOSTNAME`
- `CONFIGURATION_ENDPOINT_ADDRESS_TYPE_IPV4`

HOSTNAME 类型必须满足以下规则：

- 总长度为 `1～253` 字节。
- 名称由一个或多个点分标签组成；允许不包含点的单标签 hostname。
- 每个标签长度为 `1～63` 字节。
- 标签只允许 ASCII 字母、数字和 `-`。
- 标签不得以 `-` 开头或结尾。
- 名称不得包含空标签，也不得以点结尾。
- 整个名称必须至少包含一个 ASCII 字母；纯数字名称和 IPv4 地址字面量无效。
- `bytes[length]` 必须为 NUL `0`。

IPV4 类型必须满足以下规则：

- 地址不得为 `0.0.0.0`，首字节必须在 `1～223` 范围内。
- 地址不得属于 `127.0.0.0/8` 回环地址段或 `169.254.0.0/16` 链路本地地址段。
- 私有 IPv4 地址有效。

首选和备用服务器不得表示同一个地址。hostname 使用 ASCII 大小写不敏感比较；IPv4 使用地址值比较；
不同地址类型不视为相同地址。

违反以上任意一项 SNTP 规则时，函数返回 `CONFIGURATION_VALIDATION_SNTP_INVALID`。

### MQTT

`mqtt.mode` 只允许以下值：

- `CONFIGURATION_MQTT_MODE_DISABLED`
- `CONFIGURATION_MQTT_MODE_ENABLED`

其他值使函数返回 `CONFIGURATION_VALIDATION_MQTT_INVALID`。MQTT 禁用时，除 `mqtt.mode` 外的所有
MQTT 字段不参与校验。

MQTT 启用时，broker 必须满足以下规则：

- `broker_address` 必须是满足本规范 SNTP HOSTNAME 格式规则的 hostname；允许单标签名称。
- `broker_address` 不接受 IPv4 地址字面量。
- `broker_port` 必须位于 `1～65535` 闭区间内。

MQTT 启用时，用户名、密码和 CA 证书均为必填项。Configuration API 不提供匿名 MQTT、明文 MQTT
或省略 CA 证书的配置模式。

`mqtt.client_id.mode` 只允许以下值：

- `CONFIGURATION_CLIENT_ID_MODE_DERIVED`
- `CONFIGURATION_CLIENT_ID_MODE_EXPLICIT`

派生模式下，显式 client ID 字段不参与校验，且派生 ID 的生成方式不属于本 API 的职责。显式模式下，
client ID 长度必须为 `1～23` 字节，并且只允许 ASCII 字母和数字。

认证字段必须满足以下规则：

- `username` 长度为 `1～32` 字节，只允许可打印 ASCII `0x20～0x7E`；允许空格，但控制字符和
  非 ASCII 字符无效。
- `password` 长度为 `1～64` 字节，只允许可打印 ASCII `0x20～0x7E`；允许空格，但控制字符和
  非 ASCII 字符无效。

`mqtt.keep_alive_seconds` 必须位于 `30～3600` 秒闭区间内。

`mqtt.online_message` 和 `mqtt.will_message` 分别表示上线消息和遗嘱消息。两者的 `mode` 均只允许：

- `CONFIGURATION_MQTT_MESSAGE_MODE_DISABLED`
- `CONFIGURATION_MQTT_MESSAGE_MODE_CUSTOM`

消息禁用时，其 topic、payload、QoS 和 retain 不参与校验。CUSTOM 模式必须满足以下规则：

- topic 长度为 `1～128` 字节，只允许 ASCII 字母、数字、`.`、`_`、`-` 和 `/`。
- topic 不允许 MQTT 通配符 `+`、`#`，也不允许空格或非 ASCII 字符。
- topic 允许空层级，包括 `/status`、`status/`、`a//b` 和 `/`。
- payload 长度为 `1～128` 字节，必须是格式正确且不含内嵌 NUL 的 UTF-8；空 payload 无效。
- payload 允许 UTF-8 中合法的换行、制表符及其他控制字符，但不允许 NUL。
- QoS 必须为 `0`、`1` 或 `2`。
- retain 必须为 `0` 或 `1`。

上线消息与遗嘱消息可以使用相同的 topic。

MQTT CA 证书必须满足以下表示规则：

- 长度为 `1～4096` 字节。
- 内容是合法 PEM 文本，并且恰好包含一张证书。
- 不接受 DER 编码，也不接受包含多张证书的 CA bundle。
- 证书必须是自签名根 CA：`Basic Constraints` 必须声明 `CA = TRUE`，Subject 与 Issuer 的原始 DER
  内容必须逐字节完全相同，并且必须能够使用证书自身公钥成功验证其签名。
- `Key Usage` 扩展可以缺失；如果存在，则必须包含 `keyCertSign`。
- 中间 CA 和交叉签名证书均无效。

除验证根证书自身签名外，配置校验不检查证书当前是否过期或尚未生效，不验证外部信任链，不执行
broker hostname 匹配，也不判断密钥长度或算法强度。这些检查属于实际 TLS 握手和安全策略的职责。

解析或验证证书时，任何由临时内存不足造成的失败均返回
`CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE`。证书格式错误或不符合本节最终确定的 CA 要求时，
返回 `CONFIGURATION_VALIDATION_CERTIFICATE_INVALID`。证书使用当前固件中的 mbedTLS 无法处理的摘要、
公钥或签名算法时，同样返回 `CONFIGURATION_VALIDATION_CERTIFICATE_INVALID`；只有内存不足归类为
`CONFIGURATION_VALIDATION_RESOURCE_UNAVAILABLE`。

### Collection

- MQTT 禁用时，`collection.point_count` 必须为 `0`。
- MQTT 启用时，`collection.point_count` 必须位于 `1～16` 闭区间内。
- 采集点数组中索引大于或等于 `point_count` 的元素不参与校验。

每个有效采集点必须满足以下规则：

- `slave_address` 位于 `1～247` 闭区间内。
- `source` 只允许 Coil、Discrete Input、Holding Register 或 Input Register。
- `address` 允许完整的 `0～65535` 范围；校验不查询从站是否实际支持该地址。
- Holding Register 和 Input Register 的 `data_type` 只允许 `UINT16` 或 `INT16`。
- Coil 和 Discrete Input 的 `data_type` 不生效。
- `poll_interval_ms` 位于 `1000～3600000 ms` 闭区间内。
- `first_byte_timeout_ms` 位于 `50～3000 ms` 闭区间内。
- `first_byte_timeout_ms` 与 `poll_interval_ms` 相互独立，不要求前者小于或等于后者。
- topic 满足本规范 MQTT 自定义消息 topic 的字符规则，长度为 `1～128` 字节。
- QoS 只允许 `0` 或 `1`。

允许多个采集点读取相同的从站、source 和地址，只要它们满足 topic 唯一性规则。每个这样的采集点仍作为
一次独立事务参与总线利用率计算。

所有采集点 topic 必须区分大小写地两两不同。采集点 topic 不得等于处于 CUSTOM 状态的上线消息或
遗嘱消息 topic；处于 DISABLED 状态的消息 topic 不参与冲突检查。

违反以上 Collection 结构或采集点规则时，函数返回 `CONFIGURATION_VALIDATION_COLLECTION_INVALID`。

#### RTU 总线利用率

所有有效采集点的预计 RTU 总线利用率之和不得超过 `50%`。总和等于 `50%` 时有效；超过 `50%` 时，
函数返回 `CONFIGURATION_VALIDATION_BUS_UTILIZATION_EXCEEDED`。

每个采集点按一次请求和一次正常响应估算：

- 请求帧为 8 字节。
- Coil 和 Discrete Input 响应帧为 6 字节。
- Holding Register 和 Input Register 响应帧为 7 字节。
- `8N1` 每个字节按 10 bit 计算，`8E1`、`8O1` 和 `8N2` 每个字节按 11 bit 计算。
- 每次事务包含两个 `t3.5` 静默间隔。

设每字符 bit 数为 `bits_per_character`，RTU 波特率为 `baud_rate`：

```text
baud_rate <= 19200:
    t3.5_us = ceil(bits_per_character * 3,500,000 / baud_rate)

baud_rate > 19200:
    t3.5_us = 1750

frame_time_us = ceil(
    (request_bytes + response_bytes) * bits_per_character * 1,000,000 / baud_rate
)

transaction_time_us = 2 * t3.5_us + frame_time_us

point_utilization_us_per_second = ceil(
    transaction_time_us * 1000 / poll_interval_ms
)

total_utilization_us_per_second = sum(point_utilization_us_per_second)
```

当 `total_utilization_us_per_second <= 500000` 时利用率有效。所有除法均向上取整。

该估算不计入等待首字节超时、重试、异常响应、从站处理时间或总线空闲等待时间；它描述名义线路负载，
不描述任务调度的最坏耗时。

## `configuration_equals()`

### 前置条件

- `left` 和 `right` 均不得为 `NULL`。
- `configuration_validate(left)` 和 `configuration_validate(right)` 均必须返回
  `CONFIGURATION_VALIDATION_OK`。
- 本函数不负责再次校验输入。任一前置条件不满足时，行为未定义。

### 保证

对所有满足前置条件的配置，本函数定义的相等关系具有自反性、对称性和传递性。函数不得修改任何输入，
也不得产生其他副作用。

### 不生效字段

以下内容不参与比较：

- DHCP 模式下的所有静态网络字段；
- MQTT 禁用时除 `mqtt.mode` 外的所有 MQTT 字段；
- 派生 client ID 模式下的显式 client ID；
- 对应 MQTT 消息禁用时，该消息的 topic、payload、QoS 和 retain；
- 采集点数组中索引大于或等于 `point_count` 的元素；
- Coil 和 Discrete Input 采集点的 `data_type`；
- 所有可变长度字节字段在有效内容及其 NUL 哨兵之后的数组元素；
- C 结构体填充字节（padding）。

### 一般比较规则

除本文档明确声明为大小写不敏感或顺序不敏感的字段外，所有生效字段均按值精确比较。可变长度字节字段
必须具有相同的 `length` 和逐字节相同的有效内容；NUL 哨兵本身不参与比较。

SNTP hostname 和 MQTT broker hostname 使用 ASCII 大小写不敏感比较。例如，
`MQTT.EXAMPLE.COM` 与 `mqtt.example.com` 相等。

具体字段比较规则如下：

- Network：`mode` 精确比较。STATIC 模式下，IP 地址、子网掩码、网关和两个 DNS 地址均按 IPv4 地址值
  精确比较；DHCP 模式下这些字段忽略。
- Modbus RTU：波特率、帧格式和首字节超时均精确比较。
- Modbus TCP：监听端口精确比较。
- SNTP：两个数组位置分别比较；每个位置的地址类型必须相同。HOSTNAME 比较长度及大小写不敏感的
  ASCII 内容，IPV4 比较 IPv4 地址值；联合体中非当前类型的存储内容忽略。
- MQTT：`mode` 精确比较。DISABLED 模式下其余 MQTT 字段全部忽略。ENABLED 模式下，broker hostname
  大小写不敏感；broker 端口、client ID 模式和 keep-alive 精确比较；EXPLICIT client ID、用户名、密码和
  根证书按有效字节精确比较；DERIVED 模式下显式 client ID 忽略；上线消息和遗嘱消息分别按其角色比较。
- CUSTOM MQTT 消息：topic、payload、QoS 和 retain 均精确比较，其中 topic 和 payload 区分大小写。
  DISABLED MQTT 消息除 `mode` 外的字段忽略。
- Collection：`point_count` 精确比较，有效采集点按下文的无序规则比较。

### SNTP 服务器顺序

首选和备用 SNTP 服务器按各自位置比较，不作为无序集合处理。因此，即使两个地址本身相同，配置
`[首选 = A, 备用 = B]` 与配置 `[首选 = B, 备用 = A]` 也不相等。

### MQTT 根证书

MQTT 根证书按 PEM 字段的有效字节精确比较。只有解析后表示同一张 X.509 证书、但 PEM 文本不同的两个
字段不相等；本 API 不解析或规范化证书。

### Collection

有效采集点作为无序集合比较，采集点在数组中的排列顺序不影响结果。两份配置必须具有相同的
`point_count`，并且双方有效范围内的采集点能够一一配对。

两个采集点仅在以下所有生效字段均相等时才可配对：

- `slave_address`、`source` 和 `address`；
- 对 Holding Register 和 Input Register 生效的 `data_type`；
- `poll_interval_ms` 和 `first_byte_timeout_ms`；
- 区分大小写且逐字节比较的 topic；
- QoS。

Coil 和 Discrete Input 采集点的 `data_type` 按“不生效字段”规则忽略。

### MQTT 消息角色

上线消息与遗嘱消息按各自角色分别比较，不作为无序集合处理。将两者互换会改变配置语义，因此交换后的
配置不相等；只有两条消息自身完全相同时，交换才不会改变比较结果。

## 审核状态

- [x] `configuration_set_defaults()` 契约已审核
- [x] `configuration_validate()` 契约已审核
- [x] `configuration_equals()` 契约已审核
- [x] 整份规范已批准用于派生黑盒测试
