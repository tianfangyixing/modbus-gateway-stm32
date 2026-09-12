# Modbus RTU Transaction Scheduler 函数 API 规范

> 状态：目标契约（normative）
> 适用文件：`include/modbus_rtu_transaction_scheduler.h`、
> `src/modbus_rtu_transaction_scheduler.c`

本文档以函数为阐述对象，规定 `modbus_rtu_transaction_scheduler.h` 中公共函数
的外部契约。每个函数条目依次给出函数原型、函数行为、参数和返回值。

## 1. 模块概述

Transaction Scheduler 在一个 FreeRTOS 任务中串行执行 Modbus RTU 事务。调用方
通过高、低两个优先级的请求队列提交事务，并从对应优先级的响应队列接收结果。
一次提交只表示请求已经进入调度队列，不表示 RTU 事务已经完成或成功。

调度器本身不创建请求队列或响应队列。调用方必须分别为高、低优先级准备一组
请求/响应队列，并在启动调度器前完成绑定。两个优先级缺一不可。

本模块的公共函数不执行参数、状态或重复调用检查，也不通过返回值报告错误。
调用方必须满足本文档规定的全部前置条件；违反这些前置条件时，行为未规定。

## 2. 公共类型与队列契约

### 2.1 请求

`modbus_rtu_transaction_scheduler_request_t` 包含以下字段：

| 字段 | 含义 |
| --- | --- |
| `token` | 由调用方指定的关联标识；调度器不解释该值，并将其原样复制到响应中 |
| `response_timeout_ms` | RTU 事务等待完整响应帧的总超时时间，覆盖首字节响应时间、帧传输时间和 T3.5 分帧等待；单位为毫秒，必须大于 0 |
| `rtu_adu` | 指向待发送请求 ADU 的指针；必须是尚未释放且内容有效的池内 ADU；请求成功写入请求队列后所有权转移给调度器 |

调度器不检查 `token` 是否唯一。调用方应确保仍在等待响应的事务能够通过
`token` 明确区分。

### 2.2 响应

`modbus_rtu_transaction_scheduler_response_t` 包含以下字段：

| 字段 | 含义 |
| --- | --- |
| `token` | 对应请求的 `token` |
| `result` | `modbus_rtu_transact()` 返回的 RTU 事务结果 |
| `rtu_adu` | 与已提交请求的 `rtu_adu` 相同的池内对象；由提交该请求的模块消费响应后恰好释放一次 |

`result` 是异步 RTU 事务的结果，与调用方写入请求队列时得到的 FreeRTOS 入队
结果无关。即使 `xQueueSend()` 返回 `pdPASS`，响应中的 `result` 仍可能表示超时、
适配器错误、无效响应或其他事务错误。

### 2.3 优先级

| 枚举值 | 含义 |
| --- | --- |
| `MODBUS_RTU_TRANSACTION_SCHEDULER_PRIORITY_HIGH` | 高优先级事务 |
| `MODBUS_RTU_TRANSACTION_SCHEDULER_PRIORITY_LOW` | 低优先级事务 |

通常优先处理高优先级队列。连续处理 4 个高优先级请求后，如果低优先级队列
非空，调度器先处理 1 个低优先级请求，然后重新开始计数。如果此时低优先级
队列为空，调度器继续处理高优先级请求；高优先级队列为空时，调度器处理低
优先级请求。

### 2.4 队列与 Queue Set 要求

每个优先级都必须绑定一个请求队列和一个响应队列，共四个逻辑队列：

| 队列 | FreeRTOS 队列元素大小 |
| --- | --- |
| 高优先级请求队列 | `sizeof(modbus_rtu_transaction_scheduler_request_t)` |
| 高优先级响应队列 | `sizeof(modbus_rtu_transaction_scheduler_response_t)` |
| 低优先级请求队列 | `sizeof(modbus_rtu_transaction_scheduler_request_t)` |
| 低优先级响应队列 | `sizeof(modbus_rtu_transaction_scheduler_response_t)` |

队列长度由调用方按最大并发量确定。所有队列必须在绑定前创建成功，并在调度器
任务运行期间保持有效。每个优先级只能绑定一次，不得将同一队列重复用于不同
优先级或同时用作请求队列和响应队列。

只有在 `modbus_rtu_transaction_scheduler_start()` 已调用并返回、且调度任务成功
启动后，调用方才允许使用 `xQueueSend()` 或等价 FreeRTOS API 将请求结构按值写入
对应请求队列。`start()` 调用前及执行期间禁止向任一请求队列写入请求；其他请求
生产任务也必须通过启动顺序或同步机制遵守这一限制。是否等待队列空位由调用方
决定；从 ISR 写入时必须使用相应的 `FromISR` API，并满足 FreeRTOS 的中断优先级
要求。调度器不复制 `request.rtu_adu` 指向的对象，也不需要额外的任务通知。请求
队列不得使用 `xQueueOverwrite()` 或其他会替换已入队请求的操作，否则被替换请求
的 ADU 所有权将无法正确交还。

调用 `start()` 前，两个请求队列都必须为空，且尚未加入任何 Queue Set。`start()`
按两个请求队列当时的空闲槽数之和创建内部 Queue Set，并将两个请求队列加入该
集合。因此，请求队列在 `start()` 返回后不得再加入其他 Queue Set，也不得由
调度任务以外的消费者接收、查看后移除或重置。

工程必须同时启用 `configUSE_QUEUE_SETS` 和 `configSUPPORT_DYNAMIC_ALLOCATION`；
当前 FreeRTOS 10.3.1 的 `xQueueCreateSet()` 使用动态分配。

调度任务在每轮主循环开始时调用 `watchdog_report(WATCHDOG_EVENT_RTU_SCHEDULER)`。
看门狗必须先于调度任务完成初始化。在没有已选择请求时，调度任务以 `pdMS_TO_TICKS(500)`
调用 `xQueueSelectFromSet()`；等待超时且没有请求时进入下一轮，以便空闲期间继续报到。
被选中的 Queue Set 成员事件会按优先级累计，再按照第 2.3 节的 2:1 规则接收请求。
调度任务以 `portMAX_DELAY` 写入响应队列；调用方必须持续消费响应，否则响应队列
满时会阻塞整个调度任务。执行事务和等待响应入队期间不报到；正常完成后在下一轮主循环报到，
长时间阻塞时由看门狗检测。正常事务执行时间与响应入队等待时间之和必须留在硬件看门狗
允许的报到间隔内，调用方配置事务超时时应考虑这一约束。

## 3. 调用顺序与 ADU 所有权

### 3.1 调用顺序

调用方必须按以下顺序使用模块：

1. 调用 `modbus_rtu_adu_pool_init()` 初始化 ADU 池；
2. 调用 `modbus_rtu_transaction_scheduler_init()` 初始化调度器；
3. 调用 `modbus_rtu_transaction_scheduler_bind_queue()` 一次绑定高优先级队列；
4. 再调用一次 `modbus_rtu_transaction_scheduler_bind_queue()` 绑定低优先级队列；
5. 调用 `modbus_rtu_transaction_scheduler_start()` 启动调度任务；
6. 直接将请求写入某一优先级的请求队列，并从同一优先级的响应队列接收结果。

第 3、4 步的先后顺序可以互换，但两组队列都必须在第 5 步前绑定完成。
第 6 步只能在第 5 步完成且调度任务成功启动后开始，不得与第 5 步并发执行。
`init()` 和 `start()` 各只能调用一次；每个优先级也只能绑定一次。本模块没有
停止、解绑或反初始化接口。

### 3.2 请求和响应 ADU 的所有权

调用方使用 `modbus_rtu_adu_pool_allocate()` 分配请求 ADU，完成请求编码后，将
其地址写入 `request.rtu_adu`。

调用方将整个请求结构按值写入 FreeRTOS 请求队列，但队列不会复制
`request.rtu_adu` 指向的 ADU。只有写入 API 返回 `pdPASS` 时，请求 ADU 的所有权
才转移给调度器。调用方可以销毁或复用原请求结构变量，但不得再访问、修改或
释放其中的请求 ADU 指针，也不得因连接断开、上层超时或取消等待而提前释放该
ADU。同一个请求 ADU 不得同时用于多个已成功入队的事务。

调度器处理请求时，在任务栈上创建一个临时 ADU，以提交的池内 ADU 作为
`request`、以该临时 ADU 作为 `response` 调用 `modbus_rtu_transact()`，从而继续
满足 RTU 事务函数要求请求和响应必须是不同对象的契约。临时 ADU 无需预先清零，
其地址不会保存到响应队列。

当事务结果为 `MODBUS_RTU_TRANSACTION_OK` 或
`MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE` 时，调度器将临时 ADU 的 `length`
及该长度范围内的 `data` 复制回提交的池内 ADU，不复制临时数组未写入的尾部。
其他事务结果下，调度器不读取或复制临时 ADU 的响应内容，提交的池内 ADU 保持
原请求内容，调用方不得将其当作有效响应使用。

所有事务结果下，调度器都将 `modbus_rtu_transact()` 的实际返回值及提交时的同一
池内 ADU 指针放入对应优先级的响应队列；调度器不会为响应再分配池对象，也不会
释放提交的 ADU。因此，除请求自身占用的池槽外，即使其他池槽已经耗尽，事务也
仍可执行并返回真实结果。

提交请求的模块负责消费每个已成功提交事务的响应；即使它已经不再需要该事务
的结果，也必须取走响应，并在处理完成后恰好释放一次返回的 ADU：

~~~c
modbus_rtu_adu_pool_release(response.rtu_adu);
~~~

`response.rtu_adu` 按本模块前置条件始终是提交时的非空池内 ADU；无论
`response.result` 表示成功还是事务失败，提交请求的模块最终都必须恰好释放
一次。响应进入队列后，ADU 所有权随响应转回提交请求的模块。

如果写入 API 未返回 `pdPASS`，请求没有进入调度队列，调用方仍完全拥有请求
ADU，可以在满足相应 FreeRTOS API 使用约束的前提下重试，或者直接释放请求 ADU。

## 4. API

### 4.1 `modbus_rtu_transaction_scheduler_init`

~~~c
void modbus_rtu_transaction_scheduler_init(
    modbus_rtu_transaction_scheduler_t *scheduler,
    modbus_rtu_channel_t *rtu_channel);
~~~

**函数行为**

初始化调度器对象，保存 RTU 通道，并将高、低优先级的请求和响应队列、请求
Queue Set、任务句柄及连续高优先级请求计数器设置为初始状态。

该函数只能对一个尚未初始化的调度器调用一次。ADU 池必须已经初始化，
`rtu_channel` 必须已经正确初始化，并且调度器对象和 RTU 通道在调度任务运行
期间必须保持有效。

该函数不检查任何参数或调用状态。传入空指针、无效通道或重复初始化时，行为
未规定。

**参数**

- **`scheduler`**：待初始化的调度器对象；不得为空，且必须在调度任务运行
  期间保持有效。
- **`rtu_channel`**：调度器执行事务所使用的 RTU 通道；不得为空，必须已经
  正确初始化，并在调度任务运行期间保持有效。

**返回值**

无。该函数不报告参数错误或初始化错误。

### 4.2 `modbus_rtu_transaction_scheduler_bind_queue`

~~~c
void modbus_rtu_transaction_scheduler_bind_queue(
    modbus_rtu_transaction_scheduler_t *scheduler,
    modbus_rtu_transaction_scheduler_priority_t priority,
    QueueHandle_t request_queue,
    QueueHandle_t response_queue);
~~~

**函数行为**

将指定优先级的请求队列和响应队列保存到调度器。该函数不创建、复制或取得
队列所有权。

调用前，`scheduler` 必须已经由
`modbus_rtu_transaction_scheduler_init()` 初始化，并且尚未调用 `start()`。
高、低优先级必须各调用本函数一次；同一优先级不得重复绑定。两个优先级的
绑定顺序不限。

该函数不检查参数、调度器状态、队列元素大小、队列用途或重复绑定。违反上述
约束时，行为未规定。

**参数**

- **`scheduler`**：已经初始化且尚未启动的调度器；不得为空。
- **`priority`**：待绑定队列的优先级；只能是
  `MODBUS_RTU_TRANSACTION_SCHEDULER_PRIORITY_HIGH` 或
  `MODBUS_RTU_TRANSACTION_SCHEDULER_PRIORITY_LOW`。
- **`request_queue`**：该优先级的请求队列；不得为空，元素大小必须为
  `sizeof(modbus_rtu_transaction_scheduler_request_t)`。
- **`response_queue`**：该优先级的响应队列；不得为空，元素大小必须为
  `sizeof(modbus_rtu_transaction_scheduler_response_t)`。

**返回值**

无。该函数不报告无效参数、无效状态或重复绑定。

### 4.3 `modbus_rtu_transaction_scheduler_start`

~~~c
void modbus_rtu_transaction_scheduler_start(
    modbus_rtu_transaction_scheduler_t *scheduler,
    const char *task_name,
    UBaseType_t task_priority,
    StackType_t *task_stack,
    uint32_t task_stack_depth,
    StaticTask_t *task_buffer);
~~~

**函数行为**

函数先以两个空请求队列的长度之和调用 `xQueueCreateSet()`，再通过
`xQueueAddToSet()` 将高、低优先级请求队列加入该 Queue Set。随后使用调用方
提供的名称、优先级、栈和静态任务控制块，通过 `xTaskCreateStatic()` 创建调度
任务。任务阻塞在 `xQueueSelectFromSet()` 上，选择已经就绪的请求队列，串行取出
并执行请求，然后将响应写入对应优先级的响应队列。

调用前，调度器必须已经初始化，并且高、低优先级的请求和响应队列均已各绑定
一次。两个请求队列必须为空、不得属于其他 Queue Set，并且 FreeRTOS heap 必须
有足够空间创建容量等于两个请求队列长度之和的 Queue Set。该函数只能调用一次。
调用方提供的调度器、任务栈和静态任务控制块必须在任务运行期间保持有效。

该函数不检查参数、队列绑定状态、请求队列是否为空或重复启动，也不通过返回值
报告启动错误。Queue Set 创建失败、任一请求队列无法加入 Queue Set 或静态任务
创建失败时，函数不启动调度任务；已经创建的 Queue Set 会在需要时被删除。调用
方不得在 `start()` 返回前写入请求队列。

**参数**

- **`scheduler`**：已经初始化、完整绑定两个优先级且尚未启动的调度器；不得
  为空。
- **`task_name`**：传给 FreeRTOS 的任务名称；不得为空。
- **`task_priority`**：FreeRTOS 任务优先级；必须是系统允许的有效优先级。
- **`task_stack`**：供静态任务使用的栈存储；不得为空，并且在任务运行期间
  保持有效。
- **`task_stack_depth`**：`task_stack` 可容纳的 `StackType_t` 元素数量；必须
  大于 0，单位不是字节。
- **`task_buffer`**：供 FreeRTOS 保存静态任务控制块的存储；不得为空，并且在
  任务运行期间保持有效。

**返回值**

无。该函数不报告参数错误、状态错误或任务创建错误。
