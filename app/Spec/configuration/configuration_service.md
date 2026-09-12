# Configuration Service 函数 API 规范

> 状态：目标契约（normative）
>
> 适用文件：`Configuration/include/configuration_service.h`、
> `Configuration/src/configuration_service.c`

本文档以函数为阐述对象，规定 Configuration Service 公开 API 的可观察行为、
前置条件、参数和返回值，并约定 v2 Flash 分区、记录布局与 DMA 边界。

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
如果没有任何有效的 v2 持久化配置，函数使用当前固件的默认配置（MQTT 禁用）。
只接受 schema `0x02`；不读取旧 v1 槽地址、不迁移旧记录、不自动全片擦除。
即使只有旧 v1 记录，初始化成功后也只会得到默认配置。

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
如果错误发生在最终 magic 编程或新载荷写入后的回读校验阶段，返回 `CONFIGURATION_SERVICE_IO_ERROR`
不能用于判定新载荷是否已完成提交；后续系统启动仍可能加载该载荷。

**前置条件**

- 必须先成功调用 `configuration_service_init()`。
- 不得从 ISR 中调用。
- 调用方必须保证本次调用与本模块的其他调用以及所有其他 External Flash 访问严格串行。

**参数**

- `payload`：待持久化载荷的起始地址，不得为 `NULL`；该缓冲区至少包含 `payload_length` 字节，
  调用期间内容必须稳定且 CPU 可读，不得与 Service 内部 workspace 重叠；允许位于 CCM。
  Service 通过 CPU 复制后才调用 Flash DMA，调用方无需为此增加完整配置副本。载荷必须符合
  [Configuration Binary Codec API 与 Schema v2 规范](configuration_binary.md)，且解码后的配置模型必须有效。
- `payload_length`：载荷长度，单位为字节，必须位于
  `1..CONFIGURATION_V2_MAX_PAYLOAD_LENGTH` 闭区间内。

**返回值**

- `CONFIGURATION_SERVICE_OK`：载荷已完成持久化和回读校验；active 配置不变。
- `CONFIGURATION_SERVICE_NOT_INITIALIZED`：服务尚未成功初始化。
- `CONFIGURATION_SERVICE_INVALID_ARGUMENT`：`payload == NULL`，或 `payload_length` 不在允许范围内。
- `CONFIGURATION_SERVICE_RESOURCE_UNAVAILABLE`：校验载荷所需的临时资源不足；未访问外部 Flash。
- `CONFIGURATION_SERVICE_IO_ERROR`：访问外部 Flash 失败，或持久化后的回读内容与载荷不一致。
- `CONFIGURATION_SERVICE_INVALID_PAYLOAD`：载荷的 schema、二进制结构或配置模型无效；未访问外部 Flash。


## 3. v2 持久化与内存约束

外部 W25Q128 为 16 MiB；Service 独占最后 24 KiB。现有仓库业务代码只有 Service 使用外部 Flash；
Bootloader 应用双槽位于内部 Flash，与本分区无关。

| 项目 | 值 |
|---|---|
| 槽 A | `[0x00FFA000, 0x00FFD000)` |
| 槽 B | `[0x00FFD000, 0x01000000)` |
| 每槽容量 | 12288 字节，3 个 4096 字节扇区 |
| 头长度 | 20 字节 |
| 有效 payload 长度 | `1..8475`，仍须通过 schema v2 codec 和模型校验 |
| 最大记录 / workspace | `20 + 8475 = 8495` 字节 |
| magic | `0x32474643`，小端为 `43 46 47 32`（ASCII `CFG2`） |

头字段依次为五个 u32 LE：magic、generation、payload_length、payload_crc32、header_crc32。
payload CRC 覆盖实际 payload；头 CRC 覆盖头部前 16 字节，包含最终 CFG2 magic。
CRC 使用 CRC-32/ISO-HDLC：多项式 `0x04C11DB7`（反射表示 `0xEDB88320`）、初值与最终异或均为
`0xFFFFFFFF`、输入输出反射。未编码 NUL、数组余量与槽尾不参与 CRC。

generation 仍为三值循环 `0 → 1 → 2 → 0`。只有 `0..2` 有效；有两个有效槽时，下一代槽胜出，
相同 generation 时槽 B 胜出。首次提交写 A / generation 0，后续成功提交轮换槽并递增 generation。

读取每槽时先读 20 字节头，验证 magic、generation、头 CRC 与长度；头无效时跳过该槽，
不读 payload。头有效时只读实际 payload，再验证 payload CRC 与 codec。不能按物理槽余量
放宽 8475 字节限制。底层 I/O 或解码资源失败仍导致初始化失败，不得将访问失败伪装成无有效配置。

每次提交先完成参数、codec 与模型校验，然后按 `SLOT_SIZE / SECTOR_SIZE` 擦除目标槽的全部
3 个扇区，旧有效槽不动。CPU 将 20 字节头及 payload 复制到 workspace；先编程 offset 4 起的
头余部和 payload，最后单独编程 4 字节 magic。回读仅覆盖 `20 + payload_length` 字节，并逐字节
比较完整头与调用方保持稳定的 payload；成功后才更新本次运行期的持久化槽元信息。

中途失败或断电留下无效新槽时，下一次正常启动仍加载旧有效 v2 槽；此前无有效槽则使用默认值。
magic 实际完成后，即使驱动返回错误或回读失败，新槽也可能已有效，因此调用失败不能证明未提交。
任何写入结果都不改变当前 active。写失败后的后续有效写入仍以最近确认成功的槽元信息选择目标，
保留此前确认的有效槽。

workspace 为 4 字节对齐的普通 SRAM 静态数组（8495 字节），不使用 `.ccmdata`。
`external_flash_read()` / `external_flash_program()` 的每一个传入缓冲区都必须位于该 workspace 内。
栈上只保留 20 字节预期头用于 CPU 比较，不向驱动传入该头；Management 任务栈和输入帧可位于 CCM。
active 与临时模型仍位于 CCM，不把完整记录放到任务栈。实际 SRAM 放置须以固件链接 map 和板端 DMA 验证。
