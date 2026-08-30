# Configuration Service 函数 API 规范

> 状态：目标契约（normative）
>
> 适用文件：`Configuration/include/configuration_service.h`、
> `Configuration/src/configuration_service.c`

本文档以函数为阐述对象，规定 Configuration Service 公开 API 的可观察行为、
前置条件、参数和返回值，不规定 Flash 槽布局、持久化记录格式或内部实现方式。

## 1. 模块概述

Configuration Service 在系统启动时加载当前配置，并允许调用方将二进制配置载荷持久化，
供后续系统启动时加载。模块只公开以下三个函数：

- `configuration_service_init()`
- `configuration_service_active()`
- `configuration_service_write()`

模块生命周期只能从“未初始化”进入“可用”或“失败”状态。首次初始化失败后，
模块不能在本次系统运行期内恢复，调用方不得再调用本模块的任何 API。

### 1.1 结果类型

```c
typedef enum
{
    CONFIGURATION_SERVICE_OK = 0,
    CONFIGURATION_SERVICE_INVALID_ARGUMENT,
    CONFIGURATION_SERVICE_NOT_INITIALIZED,
    CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE,
    CONFIGURATION_SERVICE_IO_ERROR,
    CONFIGURATION_SERVICE_INVALID_PAYLOAD
} configuration_service_result_t;
```

| 枚举值 | 含义 |
|---|---|
| `CONFIGURATION_SERVICE_OK` | 操作成功完成。 |
| `CONFIGURATION_SERVICE_INVALID_ARGUMENT` | 调用参数无效。 |
| `CONFIGURATION_SERVICE_NOT_INITIALIZED` | 服务尚未成功初始化。 |
| `CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE` | 解码配置所需的临时资源不足。 |
| `CONFIGURATION_SERVICE_IO_ERROR` | 访问外部 Flash 失败，或持久化后的回读校验失败。 |
| `CONFIGURATION_SERVICE_INVALID_PAYLOAD` | 待写入载荷不是有效的 Config Binary。 |

## 2. API

### 2.1 `configuration_service_init`

```c
configuration_service_result_t configuration_service_init(void);
```

**函数行为**

首次调用时，函数从外部 Flash 加载最新的有效持久化配置作为 active 配置。
损坏、不兼容或因内容无效而被解码器拒绝的持久化数据不构成有效配置；
如果没有任何有效的持久化配置，函数使用当前固件的默认配置。

初始化成功后再次调用为幂等操作：函数直接返回成功，不再访问外部 Flash，
也不改变 active 配置。

首次初始化失败时，active 配置不可用，模块进入不可恢复的失败状态。

**前置条件**

- 首次调用前，必须已成功完成 LwIP 和 External Flash 初始化。
- 不得从 ISR 中调用。
- 调用方必须保证本次调用与本模块的其他调用以及所有其他 External Flash 访问严格串行。
- 首次调用已失败时，不得再次调用。

**参数**

无。

**返回值**

- `CONFIGURATION_SERVICE_OK`：初始化成功，或服务已成功初始化。
- `CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE`：解码持久化配置所需的临时资源不足，
  初始化失败。
- `CONFIGURATION_SERVICE_IO_ERROR`：访问外部 Flash 失败，初始化失败。

### 2.2 `configuration_service_active`

```c
const configuration_t *configuration_service_active(void);
```

**函数行为**

返回 active 配置的只读指针。初始化成功后，每次调用都返回同一个稳定指针，
指向的配置在本次系统运行期内保持不变。
`configuration_service_write()` 无论成功或失败都不会改变该指针或配置内容。

初始化成功后，函数返回当前 active 配置；否则返回 `NULL`。

**前置条件**

- 调用方只能读取返回的配置对象，不得通过移除 `const` 限定修改该对象。
- 不得从 ISR 中调用。
- 调用方必须保证本次调用与本模块的其他调用严格串行。
- 首次调用 `configuration_service_init()` 已失败时，不得调用。

**参数**

无。

**返回值**

- **非空指针**：服务已成功初始化；指向模块持有的只读 active 配置。
- **`NULL`**：服务尚未成功初始化。

### 2.3 `configuration_service_write`

```c
configuration_service_result_t configuration_service_write(const uint8_t *payload, uint32_t payload_length);
```

**函数行为**

函数先调用 `configuration_binary_decode()` 校验 `payload`。只有载荷使用受支持的 schema、二进制结构完整
且解码后的配置模型有效时，函数才将其作为新的持久化配置载荷原子提交，供后续系统启动时加载。
每次参数和载荷均有效的调用都会尝试执行一次提交，即使载荷与上一次相同也不会跳过。

载荷无效或校验所需的临时资源不足时，函数不会访问外部 Flash，也不会改变当前持久化槽位或 generation；
服务保持可用，调用方可以修正载荷或等待资源恢复后重试。

函数只在调用期间读取 `payload`，不保留该指针。无论提交成功或失败，
本次系统运行期内的 active 配置均保持不变。

提交失败时，提交前可启动的配置状态仍保持可恢复；如果此前没有有效持久化配置，
后续启动仍可使用固件默认配置。服务在写入失败后仍可继续使用。
如果错误发生在新载荷写入后的回读校验阶段，返回 `CONFIGURATION_SERVICE_IO_ERROR`
不能用于判定新载荷是否已完成提交；后续系统启动仍可能加载该载荷。

**前置条件**

- 必须先成功调用 `configuration_service_init()`。
- 不得从 ISR 中调用。
- 调用方必须保证本次调用与本模块的其他调用以及所有其他 External Flash 访问严格串行。

**参数**

- `payload`：待持久化载荷的起始地址，不得为 `NULL`；该缓冲区必须完整位于 DMA 可访问的 SRAM 中，
  在函数返回前必须保持可读，并至少包含 `payload_length` 字节。载荷必须符合
  [Configuration Binary Codec API 与 Schema v1 规范](configuration_binary.md)，且解码后的配置模型必须有效。
- `payload_length`：载荷长度，单位为字节，必须位于
  `1..CONFIGURATION_V1_MAX_PAYLOAD_LENGTH` 闭区间内。

**返回值**

- `CONFIGURATION_SERVICE_OK`：载荷已完成持久化和回读校验；active 配置不变。
- `CONFIGURATION_SERVICE_NOT_INITIALIZED`：服务尚未成功初始化。
- `CONFIGURATION_SERVICE_INVALID_ARGUMENT`：`payload == NULL`，或 `payload_length` 不在允许范围内。
- `CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE`：校验载荷所需的临时资源不足；未访问外部 Flash。
- `CONFIGURATION_SERVICE_IO_ERROR`：访问外部 Flash 失败，或持久化后的回读内容与载荷不一致。
- `CONFIGURATION_SERVICE_INVALID_PAYLOAD`：载荷的 schema、二进制结构或配置模型无效；未访问外部 Flash。
