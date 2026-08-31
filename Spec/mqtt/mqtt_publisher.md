# MQTT Publisher 运行规范

> 状态：已批准，可用于派生实现与测试

## 范围与前置条件

本文规定 MQTT Publisher 对 Configuration Service、调用任务、LwIP MQTT 和项目自有 TLS 适配可观察的
行为。Publisher 在初始化时自行调用 `configuration_service_active()`，只使用已经通过
`configuration_validate()` 且在本次运行期地址稳定的 active 配置。Publisher 不提供运行期热更新，也不得修改配置。

除非另有说明，本文中的长度均为字节数。配置中的变长文本使用其 `length` 和 `bytes` 表示；实现只可访问
生效字段，并可依赖已校验字段在 `bytes[length]` 处的 NUL 哨兵。

## 公开接口

### 状态

`mqtt_publisher_state_t` 的名称和数值必须保持兼容：

| 名称 | 数值 | 含义 |
|---|---:|---|
| `MQTT_PUBLISHER_STATE_DISABLED` | 0 | MQTT 未初始化或 active 配置明确禁用 MQTT |
| `MQTT_PUBLISHER_STATE_DISCONNECTED` | 1 | 已启用但当前未连接，或正在等待网络、校时等前置条件 |
| `MQTT_PUBLISHER_STATE_CONNECTING` | 2 | 正在创建 TLS 配置、解析 broker 或已发起 MQTT 连接 |
| `MQTT_PUBLISHER_STATE_CONNECTED` | 3 | 最近一次连接回调已报告 CONNACK 接受 |
| `MQTT_PUBLISHER_STATE_ERROR` | 4 | 初始化失败，或本轮 TLS 配置、DNS 解析失败 |

`mqtt_publisher_get_state()` 可从任务上下文读取当前状态；返回值是调用瞬间的快照。

### 初始化

`mqtt_publisher_init(void)` 不接收参数，也不返回初始化结果。Publisher 必须自行获取 active 配置、判断是否
启用，并通过状态与不含敏感信息的日志反映结果；调用方不得根据额外返回值决定是否启动 Publisher。

首次调用时：

- `configuration_service_active()` 返回 `NULL`，或 active MQTT `mode` 不是已定义值时，不创建任何队列、
  任务、TLS 配置或 MQTT client；状态设为 `ERROR`，记录错误并调用项目现有 `Error_Handler()`。
- `mode == CONFIGURATION_MQTT_MODE_DISABLED` 时状态保持 `DISABLED`，不得访问其他 MQTT 字段，不创建发布
  槽互斥量、连接任务、TLS 配置或 MQTT client，并记录配置已禁用。
- `mode == CONFIGURATION_MQTT_MODE_ENABLED` 时保存 `&configuration_service_active()->mqtt` 的稳定只读地址，
  创建发布槽互斥量、单个 MQTT client 和连接任务；初始状态为 `DISCONNECTED`。只有全部资源均创建成功后，
  连接任务才可开始运行。
- 互斥量、MQTT client 或任务创建失败时状态设为 `ERROR`，记录错误并调用 `Error_Handler()`；已创建的部分资源必须撤销或
  保持不可运行，禁止留下会连接 broker 或接受发布的孤立任务。

一次成功启用或成功禁用都完成初始化。此后的重复调用只记录已经初始化并直接返回，不得更换配置、重建资源或
改变状态。初始化失败由 `Error_Handler()` 终止正常启动流程，不向调用方暴露初始化结果值。

初始化必须由普通任务在调度器启动后的系统启动阶段调用，且必须早于任何 Collection 初始化。

### 异步发布接口

通用发布接口必须由普通 FreeRTOS 任务调用，并接收：

- NUL 结尾的 topic 及其可判定长度，合法长度为 `1～128`；
- payload 指针和 `0～128` 的长度；长度非零时指针不得为 `NULL`；
- QoS `0～2`；
- retain `0` 或 `1`；
- 可为 `NULL` 的完成回调和原样传回回调的 `context`。

接口必须立即返回可判定的提交结果，区分已接纳、参数无效、禁用、未连接和无发布槽。
`MQTT_PUBLISHER_PUBLISH_OK` 只表示请求已被 LwIP 接纳，不表示发布已经完成。不得从 ISR、LwIP TCPIP 回调或
Publisher 完成回调中调用。Collection 固定传 `retain = 0`；保留 retain 参数是为了其他受控发布用途，而不是
允许调用方绕过配置。

`mqtt_publish()` 在成功返回前把 topic 和 payload 复制进 LwIP 输出环形缓冲，因此公开接口返回后调用方可立即
复用或释放输入。提交失败不调用完成回调；提交成功且回调非 `NULL` 时必须恰好调用一次：LwIP 完成映射为
`OK`，LwIP 请求超时映射为 `TIMEOUT`，连接断开映射为 `NOT_CONNECTED`。
完成回调运行在 Publisher/LwIP 回调路径中，必须短时、不可阻塞，通常只应投递通知给业务任务。

Publisher 维护 4 个静态发布槽，并发调用通过槽互斥量分配 callback/context；无空闲槽时立即返回
`NO_RESOURCE`，不得无界积压。槽只在 LwIP 已接纳请求后保持占用；提交失败必须立即释放且不得调用完成回调。
连接丢失时，全部在途槽必须以 `NOT_CONNECTED` 终结。

LwIP 在调用连接断开回调前先清空 client 的请求表；Publisher 必须随后释放并完成全部在途槽。完成路径必须
先在锁内清空对应槽，再在锁外调用用户回调，防止回调重入破坏槽表。

## 配置字段映射

MQTT 启用时，全部生效字段按下列规则应用；不得用编译期常量、默认凭据或内置证书替换：

- `broker_address`：同一个 NUL 结尾 hostname 同时用于 DNS、连接日志、TLS SNI 和证书身份校验。
- `broker_port`：原值传给 `mqtt_client_connect()`，不固定为 8883。
- `client_id`：EXPLICIT 模式逐字节使用配置值；DERIVED 模式使用下节规定的设备 UID 编码。
- `username` 与 `password`：分别映射为 `client_user` 和 `client_pass`，不得交换、截断或记录。
- `ca_certificate_pem`：是唯一信任根来源。传给 PEM 解析接口时必须包含配置中的有效内容和末尾 NUL，因而
  长度参数为配置长度加一。
- `keep_alive_seconds`：原值映射到 MQTT CONNECT 的 `keep_alive`。
- `will_message`：CUSTOM 时在 CONNECT 中设置 topic、payload、QoS 和 retain；DISABLED 时必须将 will topic
  和 payload 设为 `NULL`，且不得访问其余字段。
- `online_message`：CUSTOM 时在成功连接回调中直接异步提交一次；DISABLED 时不得访问其余字段，也不发送替代消息。

Publisher 只保存 active MQTT 配置的只读指针，LwIP、TLS、will 和 online message 路径直接读取该配置；不得
建立第二份 runtime config，也不得复制配置后接受运行期替换。派生 Client ID 使用 Publisher 自有固定缓冲区，
其余指向配置字节的指针在 Publisher 运行期内均保持有效。

## 派生 Client ID

DERIVED 模式必须读取 `HAL_GetUIDw0()`、`HAL_GetUIDw1()` 和 `HAL_GetUIDw2()`，按 w0、w1、w2 顺序把每个
32-bit word 序列化为四个 big-endian 字节，得到 12 字节 UID。再以 RFC 4648 大写 Base32 字母表
`ABCDEFGHIJKLMNOPQRSTUVWXYZ234567`、不带 `=` padding 编码为 20 个字符，并在前面添加 `STM`。

结果必须是 23 个 ASCII 字母数字字符和一个 NUL，写入 Publisher 内部固定 24 字节缓冲区。最后一个 Base32
字符的低四位来自补零，只允许编码结果中的有效 96 bit 参与语义；不得丢弃 UID 位，不得使用 MAC 地址、随机数
或固定 ID 替代。生产接口不公开通用编码函数；主机测试构建必须能通过专用测试钩子注入三个 32-bit word，且
该构建只编译纯编码逻辑，不依赖目标平台 HAL、FreeRTOS、LwIP 或 TLS。

## TLS 安全策略

TLS 连接必须 fail-closed：

- 没有 active 配置 CA、TLS 配置创建失败或 hostname 无法设置时，不得回退到明文连接、内置 CA、禁用 SNI
  或跳过证书身份校验；本次连接失败并进入清理/退避重连。
- 项目自有适配必须对 client endpoint 强制使用 `MBEDTLS_SSL_VERIFY_REQUIRED`，即使 vendor 适配请求
  `MBEDTLS_SSL_VERIFY_OPTIONAL`。
- 每个 client SSL context 在握手前必须以同一个 active broker hostname 调用
  `mbedtls_ssl_set_hostname()`，从而同时启用 SNI 和证书主机名校验。
- `mbedtls_ssl_handshake()` 返回成功后仍必须检查 `mbedtls_ssl_get_verify_result()`；结果非零视为
  `MBEDTLS_ERR_X509_CERT_VERIFY_FAILED`，连接不得进入 MQTT CONNECTED 状态。
- DNS、TLS SNI、证书身份校验和可记录的 broker 名称之间不得使用不同字符串。

TLS hostname 的受控设置必须在创建或使用对应 TLS client context 前完成。系统只允许一个 Publisher 使用该
策略；清理连接不得让旧 hostname 泄漏到下一次初始化或其他 TLS 使用者。

## 连接、上线和重试

Publisher 在初始化时创建一个 MQTT client，并在整个运行期复用。连接任务每 15 秒执行一次，并在检查
`connected` 前先检查网络接口是否存在、接口是否 up 以及链路是否 up。连接任务检测到任一网络条件不满足时，
Publisher 必须把 `connected` 置假、状态设为 `DISCONNECTED`，在 TCPIP core lock 内调用 `mqtt_disconnect()`
清理现有连接，
并以 `NOT_CONNECTED` 逐个终结全部占用的普通发布槽。主动断开不依赖连接回调，也不释放或重建 MQTT client；
网络条件恢复前，连接任务保持等待。

网络条件满足且 `connected` 为真时，连接任务只延时；否则继续检查 SNTP 已同步，确保 active CA 对应的 TLS 配置
可用并解析 active broker hostname，然后调用一次 `mqtt_client_connect()` 并检查其同步返回值；只有 `ERR_OK` 才
表示 TCP/TLS/MQTT 异步连接已经启动。其他返回值必须记录错误并保持未连接；`ERR_ISCONN` 表示上一次异步连接
仍停留在 MQTT client 中，任务必须在 TCPIP core lock 内调用 `mqtt_disconnect()` 销毁对应 PCB。下一轮仍重新
检查网络条件、`connected`、DNS 和连接条件；重试周期保持 15 秒，不实施指数退避。

连接回调收到 `MQTT_CONNECT_ACCEPTED` 时把 `connected` 置真并把状态设为 `CONNECTED`；其他状态均把
`connected` 置假、状态设为 `DISCONNECTED`，并以 `NOT_CONNECTED` 逐个终结全部占用的普通发布槽。断开后
LwIP 已在调用失败或断开回调前关闭 PCB 并把 MQTT client 置为 `TCP_DISCONNECTED`，连接任务会在下一次 15 秒
周期重新执行 TLS/DNS/连接步骤。

系统启动必须在 `MX_LWIP_Init()` 前使用 STM32 硬件 RNG 为 LwIP 平台 `rand()` 播种，使自动分配的 TCP 临时端口
不会在每次复位后重复同一序列。项目还必须通过 `LWIP_HOOK_TCP_ISN` 为每条新 TCP 连接提供硬件随机初始序列号，
避免未正常关闭的 broker 旧连接与复位后的新连接复用相同四元组和 ISN。实现只能位于项目自有 `LWIP/Target/`
和 `lwipopts.h`，不得修改 LwIP vendor 源码。

will 参数在初始化时从稳定 active 配置写入 CONNECT 参数。每个成功 CONNACK 都在连接回调中直接异步提交一次
启用的 online message；提交结果只记录日志，不阻塞连接成立，也不触发额外重试。普通发布失败不缓存旧 payload，
不自动在新连接重放；调用方自行等待其下一业务周期。TLS 配置和 MQTT client 均按 Publisher 运行期持有，
Publisher 不拥有 Configuration Service 的配置内存。

普通发布在取得 TCPIP core lock 后必须再次检查 `connected`；若连接已经失效，则释放 core lock 并直接返回
`NOT_CONNECTED`，不得占用发布槽或调用 `mqtt_publish()`。该检查用于覆盖调用方通过锁外快速检查后，连接任务已经
开始主动断开的情况。

## LwIP 输出环形缓冲要求

按 Configuration 合法上限计算，MQTT 3.1.1 CONNECT 的 remaining length 最大为：

```text
固定可变头                         10
client ID              2 + 23 =   25
will topic/payload      2 + 128 + 2 + 128 = 260
username                2 + 32 =   34
password                2 + 64 =   66
remaining length 合计             395
固定头（类型 1 + 变长长度 2）       3
CONNECT 总计                       398 bytes
```

受控通用 PUBLISH 以 topic 128、payload 128、QoS 1/2 的 packet ID 2 计算，remaining length 为
`2 + 128 + 2 + 128 = 260`，加 3 字节固定头后为 263 bytes。QoS 0 更小。

因此项目配置必须保证 `MQTT_OUTPUT_RINGBUF_SIZE >= 512`，以容纳任一最大合法 CONNECT 或 PUBLISH，并保留
明确的 2 次幂容量。只能在项目自有 `lwipopts.h` 配置该值，禁止修改 `Middlewares/**`。4 个发布槽是并发上限，
不是环形缓冲容量保证；LwIP 空间不足时当前提交返回 `NO_RESOURCE`，不得另建离线缓存。

## 并发、所有权与失败

- 初始化创建唯一 MQTT client，连接任务只复用它发起连接；所有任务上下文中的 LwIP MQTT API 调用均在
  TCPIP core lock 保护下进行，LwIP 回调沿用其已有 core 上下文。
- 发布输入由调用方拥有至 LwIP 完成复制；公开接口返回后 Publisher 不再引用调用方输入。
- LwIP 回调必须先释放受保护的发布槽再调用用户回调；用户回调不得阻塞。
- Publisher 的连接任务栈、4 个请求槽和同步对象必须静态或有界；不得增加无界队列、动态消息积压或无限重试。
- 参数错误、未连接、ADU/业务失败或发布失败都不得复用旧 payload，也不得在内部形成离线缓存。

## 日志与敏感信息

日志可以包含非敏感状态、active broker hostname、端口、MQTT/LwIP/mbedTLS 错误码、请求序号和采集点索引。
日志不得包含 password、CA PEM、完整 CONNECT 凭据或 payload；username 和 Client ID 也不得作为错误诊断的
必要内容输出。DNS 结果可以记录 IP 地址，但 TLS 身份始终使用 broker hostname。

## 禁止修改边界

本规范通过项目自有 Publisher、TLS 包装翻译单元和 `LWIP/Target/lwipopts.h` 实现。`Middlewares/**` 是绝对
只读边界，不得修改 LwIP MQTT、ALTCP TLS 或 mbedTLS vendor 源来满足本规范。
