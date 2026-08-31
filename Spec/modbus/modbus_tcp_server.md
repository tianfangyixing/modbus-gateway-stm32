# Modbus TCP Server API 与行为规范

> 状态：目标契约（normative）
> 适用文件：`include/modbus_tcp_server.h`、`src/modbus_tcp_server.c`

本文档规定当前 Modbus TCP Server 公共 API、任务状态机、Modbus TCP 与 RTU
之间的转换、队列及 ADU 所有权约定。调用方不得依赖本文档未规定的内部字段或
执行顺序。

## 1. 模块职责

服务器在一个 FreeRTOS 任务中使用 LwIP socket API：

- 监听一个 Modbus TCP 端口；
- 同时维护最多 4 个客户端连接；
- 增量接收并校验 MBAP 头及完整 TCP ADU；
- 将合法 TCP 请求转换成带 CRC 的 RTU 请求；
- 通过 RTU 事务调度器的高优先级队列串行执行请求；
- 将 RTU 正常响应或异常响应转换回 TCP 响应；
- 对本地校验错误、网关错误和目标设备错误生成 Modbus 异常响应。

服务器不创建或启动 RTU 调度器，也不创建或绑定高、低优先级队列。高优先级请求/响应队列由调用方创建、
绑定并通过配置注入，服务器仅在运行期间借用；服务器不支持停止、重启、反初始化或运行期间修改配置。

## 2. 常量与公开状态

### 2.1 常量

| 常量 | 值 | 含义 |
| --- | ---: | --- |
| `MODBUS_TCP_SERVER_MAX_CLIENTS` | 4 | 同时保存的客户端连接上限 |
| `MODBUS_TCP_ADU_MAX_LENGTH` | 260 | 6 字节 MBAP 前缀加 `Length` 字段所声明内容的最大总长度 |
| `MODBUS_TCP_RESPONSE_TIMEOUT_MIN_MS` | 50 | 允许配置的最小 RTU 响应超时 |
| `MODBUS_TCP_RESPONSE_TIMEOUT_MAX_MS` | 60000 | 允许配置的最大 RTU 响应超时 |

MBAP `Length` 字段必须位于 `2..254`。完整 TCP ADU 的字节数为
`6 + Length`，因此最大为 260 字节。

### 2.2 `modbus_tcp_server_result_t`

| 枚举值 | 含义 |
| --- | --- |
| `MODBUS_TCP_SERVER_OK` | 初始化并启动成功 |
| `MODBUS_TCP_SERVER_INVALID_ARGUMENT` | 必需参数为空或配置值超出允许范围 |
| `MODBUS_TCP_SERVER_INVALID_STATE` | 当前服务器状态不允许执行该操作 |
| `MODBUS_TCP_SERVER_TASK_CREATE_FAILED` | 静态服务器任务创建失败 |

### 2.3 `modbus_tcp_server_state_t`

| 枚举值 | 含义 |
| --- | --- |
| `MODBUS_TCP_SERVER_STATE_UNINITIALIZED` | 尚未成功初始化 |
| `MODBUS_TCP_SERVER_STATE_RUNNING` | 服务器任务已创建并开始运行 |
| `MODBUS_TCP_SERVER_STATE_FAILED` | 服务器任务创建失败，不允许使用同一对象重试初始化 |

### 2.4 客户端状态

每个已接受的连接在任一时刻处于以下一种状态：

| 状态 | 行为 |
| --- | --- |
| `MODBUS_TCP_SERVER_CLIENT_STATE_RECEIVING_TCP_REQUEST` | 接收并组装一个 TCP 请求 |
| `MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_SUBMIT` | 持有请求 ADU，等待高优先级请求队列出现空位 |
| `MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_RESPONSE` | 请求所有权已转移给调度器，等待匹配 token 的响应 |
| `MODBUS_TCP_SERVER_CLIENT_STATE_SENDING_TCP_RESPONSE` | 分段发送正常响应或本地异常响应 |

同一连接一次只处理一个请求，不并行处理或重排流水化请求。客户端提前发送的
后续字节保留在 socket 接收缓冲区中，直至当前响应发送完成。

## 3. 配置与生命周期

### 3.1 `modbus_tcp_server_config_t`

| 字段 | 约束 |
| --- | --- |
| `request_queue` | 已绑定到运行中调度器的高优先级请求队列；不得为空，元素类型必须为 `modbus_rtu_transaction_scheduler_request_t` |
| `response_queue` | 与 `request_queue` 成对的高优先级响应队列；不得为空，元素类型必须为 `modbus_rtu_transaction_scheduler_response_t` |
| `listen_port` | TCP 监听端口；必须大于 0 |
| `response_timeout_ms` | RTU 完整响应超时；必须位于 `50..60000` 毫秒 |
| `task_name` | 服务器任务名称；不得为空 |
| `task_priority` | FreeRTOS 任务优先级；必须小于 `configMAX_PRIORITIES` |
| `task_stack` | 静态任务栈；不得为空，生命周期必须覆盖服务器任务 |
| `task_stack_depth` | 任务栈深度；必须大于 0，单位为 `StackType_t` 元素 |
| `task_buffer` | 静态任务控制块；不得为空，生命周期必须覆盖服务器任务 |

配置结构本身只需在 `modbus_tcp_server_init()` 调用期间保持有效。请求/响应队列及其静态存储、任务栈、
任务控制块和服务器对象必须在服务器任务的整个生命周期内保持有效。

### 3.2 调用顺序

调用方必须按以下顺序集成服务器：

1. 初始化 RTU ADU 池、物理通道和 RTU 通道；
2. 调用 `modbus_rtu_transaction_scheduler_init()`；
3. 由集成层创建高、低优先级请求和响应队列；
4. 将两组队列分别绑定到调度器；
5. 启动 RTU 调度器并确认调度任务创建成功；
6. 通过配置传入高优先级请求和响应队列并调用 `modbus_tcp_server_init()`；该调用同时启动服务器任务。

服务器对象必须预先零初始化且只能调用一次 `init()`。初始化成功后，调用方不得直接修改服务器对象、
内部客户端状态或操作注入队列；队列只能由服务器和 RTU 调度器按本规范约定使用。

## 4. 公共函数

### 4.1 `modbus_tcp_server_init`

```c
modbus_tcp_server_result_t modbus_tcp_server_init(modbus_tcp_server_t *server,
                                                  const modbus_tcp_server_config_t *config);
```

**函数行为**

检查配置并初始化、启动服务器。配置有效时，函数依次：

1. 清零服务器运行时字段并保存监听端口、RTU 响应超时以及两个外部队列句柄；
2. 初始化 token、监听 socket 和所有客户端 socket 状态；
3. 创建静态 TCP 服务器任务；新任务开始运行后立即尝试创建监听 socket；
4. 将服务器状态设置为 `RUNNING`。

服务器不创建、绑定、复位或删除注入的队列。任务创建失败时清除服务器保存的队列句柄，将状态设置为
`FAILED` 并返回失败；调用方仍拥有队列及其静态存储。

**参数**

- **`server`**：接收服务器状态和全部静态资源；不得为空。
- **`config`**：初始化配置；不得为空，且所有字段必须满足第 3.1 节约束。

**返回值**

- **`MODBUS_TCP_SERVER_OK`**：初始化成功，服务器处于 `RUNNING`。
- **`MODBUS_TCP_SERVER_INVALID_ARGUMENT`**：`server`、`config` 或任一必需配置
  字段非法。
- **`MODBUS_TCP_SERVER_INVALID_STATE`**：服务器对象不是零初始化的 `UNINITIALIZED` 状态，包括重复初始化或
  任务创建失败后重试。
- **`MODBUS_TCP_SERVER_TASK_CREATE_FAILED`**：服务器任务创建失败。

## 5. 监听与连接管理

服务器任务开始运行后创建 IPv4 TCP socket，绑定 `INADDR_ANY` 和配置端口，
以 backlog 1 开始监听，并将监听 socket 设置为非阻塞。任一步失败时关闭已有
监听 socket，等待 50 ms 后重新尝试。

服务器使用最长 10 ms 的 `select()` 周期处理监听 socket 和客户端 socket。
`select()` 被信号中断时立即重试；发现无效描述符时关闭对应连接，监听描述符
失效时关闭全部客户端并重新创建监听 socket。

接受连接后，服务器要求下列 socket 选项全部设置成功，否则立即关闭连接：

- 非阻塞模式；
- `SO_KEEPALIVE` 开启；
- `TCP_KEEPIDLE = 60` 秒；
- `TCP_KEEPINTVL = 10` 秒；
- `TCP_KEEPCNT = 3`。

已有 4 个客户端时，新接受的连接立即关闭，不影响现有连接。

## 6. TCP 请求接收与校验

服务器先精确接收 7 字节 MBAP 头，再根据 `Length` 字段接收剩余字节。一次
`recv()` 不会请求超过当前帧剩余容量的字节，因此不会把下一帧字节并入当前帧。

收到第一个字节后，如果连续 5000 ms 没有收到后续字节，则关闭连接。尚未收到
首字节的空闲连接没有应用层读取超时，只受 TCP keepalive 和对端行为影响。

完整 MBAP 头必须满足：

- Protocol Identifier 等于 `0x0000`；
- `Length` 位于 `2..254`；
- 完整帧长度等于 `6 + Length`。

协议标识符或长度字段非法时直接关闭连接，不生成 Modbus 异常响应。

头部有效且完整帧到达后，服务器从 ADU 池分配请求对象，将 Unit Identifier 和
PDU 复制为 RTU 地址、功能码及数据，并计算、追加低字节在前的 CRC。随后调用
`modbus_rtu_validate_request()`。

请求校验错误映射如下：

| RTU 校验结果 | TCP 异常码 |
| --- | ---: |
| `MODBUS_RTU_FUNCTION_UNSUPPORTED` | `0x01` Illegal Function |
| `MODBUS_RTU_ADDRESS_RANGE_INVALID` | `0x02` Illegal Data Address |
| `MODBUS_RTU_LENGTH_INVALID`、`MODBUS_RTU_BYTE_COUNT_INVALID`、`MODBUS_RTU_VALUE_INVALID`、`MODBUS_RTU_QUANTITY_INVALID` | `0x03` Illegal Data Value |
| `MODBUS_RTU_SLAVE_ADDRESS_INVALID` | `0x0A` Gateway Path Unavailable |
| 其他失败 | `0x04` Server Device Failure |

请求 ADU 分配失败时同样返回异常码 `0x04`。

## 7. 调度、token 与 ADU 所有权

合法请求获得一个按 `uint32_t` 自然回绕的递增 token。服务器以 0 tick 等待时间
调用 `xQueueSend()`，将请求结构直接写入调度器的高优先级请求队列。请求队列已满
时，客户端保持 `MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_SUBMIT`，服务器
在之后的事件循环中重试；不需要另行通知调度任务，Queue Set 会使其就绪。

- `xQueueSend()` 返回 `pdPASS` 前，请求 ADU 由服务器客户端状态拥有；连接在此
  期间关闭时，服务器负责释放它。
- `xQueueSend()` 返回 `pdPASS` 后，请求 ADU 所有权转移给调度器，服务器立即
  清除本地 ADU 指针。
- 调度器在所有事务结果下都返回提交时的同一池内 ADU；无论事务成功或失败，
  服务器在复制或处理完成后都恰好释放一次该对象。

服务器只消费响应队列头部与某个
`MODBUS_TCP_SERVER_CLIENT_STATE_WAITING_FOR_RTU_RESPONSE` 客户端 token 匹配的
响应。连接不再存在且没有客户端等待该 token 时，服务器取出遗留响应并恰好
释放一次其中的池内 ADU。

## 8. RTU 响应转换

调度器响应映射如下：

| 事务结果 | TCP 行为 |
| --- | --- |
| `MODBUS_RTU_TRANSACTION_OK` | 转发正常响应 |
| `MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE` | 转发从站异常响应 |
| `MODBUS_RTU_TRANSACTION_ADAPTER_IO_ERROR` | 生成异常码 `0x0A` |
| `MODBUS_RTU_TRANSACTION_RESPONSE_TIMEOUT`、`MODBUS_RTU_TRANSACTION_RESPONSE_LENGTH_INVALID`、`MODBUS_RTU_TRANSACTION_RESPONSE_CRC_INVALID`、`MODBUS_RTU_TRANSACTION_RESPONSE_SLAVE_ADDRESS_MISMATCH`、`MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_MISMATCH`、`MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_INVALID`、`MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH`、`MODBUS_RTU_TRANSACTION_RESPONSE_DATA_INVALID` | 生成异常码 `0x0B` Gateway Target Device Failed to Respond |
| 其他事务结果 | 生成异常码 `0x04` |

转发 RTU 响应时，服务器移除最后两个 CRC 字节，使用原 TCP 请求的 Transaction
Identifier，Protocol Identifier 固定为 0，并把 RTU 地址及 PDU 写入 MBAP 头之后。
MBAP `Length` 等于 RTU 响应长度减去 2 个 CRC 字节。

本地异常响应固定为 9 字节：MBAP `Length` 为 3，Unit Identifier 与请求一致，
响应功能码为请求功能码按位或 `0x80`，最后一个字节为异常码。

## 9. TCP 响应发送与关闭

服务器使用非阻塞 `send()`，允许一个响应分多次发送。发送计时从响应准备完成
开始；5000 ms 内未发送完整响应时关闭连接。完整发送后，客户端回到
`MODBUS_TCP_SERVER_CLIENT_STATE_RECEIVING_TCP_REQUEST` 并开始接收下一帧。

连接在等待 RTU 提交或响应期间不加入读取集合，因此对端断开通常会在服务器
准备发送响应后由 `send()` 错误发现。该行为不会改变已经转移给调度器的请求
所有权；对应响应仍会被消费或作为遗留响应清理。

服务器关闭连接时将 socket 设置为无效值。除仍由服务器持有、尚未提交的请求
ADU 外，不释放已经转移给调度器的对象。

## 10. 并发与调用限制

- 服务器任务是服务器对象及客户端状态的唯一运行时写入者。
- `init()` 只能由任务上下文调用，不得从 ISR 调用。
- 调用方必须在 `init()` 前把注入的高优先级请求队列绑定到调度器一次，并在服务器运行期间保持请求、
  响应队列及其存储有效。
- RTU 调度器必须在服务器 `init()` 前成功启动，使高优先级请求队列已经加入其 Queue Set。
- 调用方必须持续保证 ADU 池、调度器、队列、任务栈、任务控制块和服务器对象有效。
- 当前实现不提供取消已提交 RTU 事务的机制。
