# W25Q128 外部 Flash 函数 API 规范

> 状态：目标契约（normative）
> 适用文件：`ExternalFlash/include/external_flash.h`、`ExternalFlash/src/external_flash.c`

本文档规定 W25Q128 外部 Flash 驱动的函数作用、参数和返回值，不规定具体实现方式。

本驱动不可重入。任一接口调用返回前，其他任务或中断不得再次调用本驱动的任何接口；调用方必须保证所有外部 Flash 访问严格串行。驱动接口不得在 ISR 中调用。

请务必保证使用的buffer能被dma访问

## 1. 结果类型

```c
typedef enum
{
    EXTERNAL_FLASH_RESULT_OK = 0,
    EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT,
    EXTERNAL_FLASH_RESULT_NOT_INITIALIZED,
    EXTERNAL_FLASH_RESULT_OUT_OF_RANGE,
    EXTERNAL_FLASH_RESULT_BUSY,
    EXTERNAL_FLASH_RESULT_TIMEOUT,
    EXTERNAL_FLASH_RESULT_IO_ERROR
} external_flash_result_t;
```

| 枚举值 | 含义 |
|---|---|
| `EXTERNAL_FLASH_RESULT_OK` | 操作成功完成。 |
| `EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT` | 参数无效，包括空指针、零长度、不满足要求的缓冲区或未按 4 KiB 对齐的擦除地址。 |
| `EXTERNAL_FLASH_RESULT_NOT_INITIALIZED` | 驱动尚未成功初始化。 |
| `EXTERNAL_FLASH_RESULT_OUT_OF_RANGE` | 请求访问的地址范围超出 W25Q128 的 `0x00000000..0x00FFFFFF`，或地址与长度计算发生溢出。 |
| `EXTERNAL_FLASH_RESULT_BUSY` | 外部 Flash 驱动所使用的通信资源当前忙，无法开始操作。 |
| `EXTERNAL_FLASH_RESULT_TIMEOUT` | 操作未在规定时间内完成。 |
| `EXTERNAL_FLASH_RESULT_IO_ERROR` | 操作期间发生通信错误。 |

## 2. 公共 API

### 2.1 `external_flash_init`

```c
external_flash_result_t external_flash_init(void);
```

**函数作用**

初始化外部 Flash 驱动，使读、编程和擦除接口可用。重复调用时，如果驱动已经成功初始化，则返回成功。

**参数**

无。

**返回值**

- `EXTERNAL_FLASH_RESULT_OK`：初始化成功，或驱动已经初始化。
- `EXTERNAL_FLASH_RESULT_BUSY`：驱动所使用的通信资源当前忙。
- `EXTERNAL_FLASH_RESULT_IO_ERROR`：初始化失败。

### 2.2 `external_flash_program`

```c
external_flash_result_t external_flash_program(uint32_t start_addr, const uint8_t *source, uint32_t length);
```

**函数作用**

将 `source` 指向的 `length` 字节数据编程到从 `start_addr` 开始的连续外部 Flash 地址。该函数不负责预先擦除目标区域。返回成功表示全部数据均已完成编程，且外部 Flash 已恢复为就绪状态。

**参数**

- `start_addr`：目标区域的起始字节地址，必须位于 `0x00000000..0x00FFFFFF`。
- `source`：源数据缓冲区，不能为空；缓冲区必须完整位于 DMA 可访问的 SRAM 中，并至少包含 `length` 字节。
- `length`：待编程的字节数，必须大于 0；`start_addr` 至 `start_addr + length - 1` 必须全部位于有效地址范围内。

**返回值**

- `EXTERNAL_FLASH_RESULT_OK`：全部数据编程成功。
- `EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT`：`source` 为空、`length` 为 0，或源缓冲区不满足要求。
- `EXTERNAL_FLASH_RESULT_NOT_INITIALIZED`：驱动尚未初始化。
- `EXTERNAL_FLASH_RESULT_OUT_OF_RANGE`：目标地址范围越界或地址计算溢出。
- `EXTERNAL_FLASH_RESULT_BUSY`：驱动所使用的通信资源当前忙。
- `EXTERNAL_FLASH_RESULT_TIMEOUT`：等待操作完成超时。
- `EXTERNAL_FLASH_RESULT_IO_ERROR`：操作期间发生通信错误。

### 2.3 `external_flash_read`

```c
external_flash_result_t external_flash_read(uint32_t start_addr, uint8_t *destination, uint32_t length);
```

**函数作用**

等待外部 Flash 就绪，然后读取从 `start_addr` 开始的连续 `length` 字节，并写入 `destination` 指向的缓冲区。返回成功表示目标数据已全部读入缓冲区。

**参数**

- `start_addr`：源区域的起始字节地址，必须位于 `0x00000000..0x00FFFFFF`。
- `destination`：目标数据缓冲区，不能为空；缓冲区必须完整位于 DMA 可访问的 SRAM 中，并至少可写入 `length` 字节。
- `length`：待读取的字节数，必须大于 0；`start_addr` 至 `start_addr + length - 1` 必须全部位于有效地址范围内。

**返回值**

- `EXTERNAL_FLASH_RESULT_OK`：全部数据读取成功。
- `EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT`：`destination` 为空、`length` 为 0，或目标缓冲区不满足要求。
- `EXTERNAL_FLASH_RESULT_NOT_INITIALIZED`：驱动尚未初始化。
- `EXTERNAL_FLASH_RESULT_OUT_OF_RANGE`：源地址范围越界或地址计算溢出。
- `EXTERNAL_FLASH_RESULT_BUSY`：驱动所使用的通信资源当前忙。
- `EXTERNAL_FLASH_RESULT_TIMEOUT`：等待就绪或读取操作完成超时。
- `EXTERNAL_FLASH_RESULT_IO_ERROR`：操作期间发生通信错误。

### 2.4 `external_flash_erase_4k`

```c
external_flash_result_t external_flash_erase_4k(uint32_t sector_address);
```

**函数作用**

擦除以 `sector_address` 为起始地址的一个 4 KiB 扇区。返回成功表示整个扇区已完成擦除，且外部 Flash 已恢复为就绪状态。

**参数**

- `sector_address`：待擦除扇区的起始字节地址，必须按 4 KiB 对齐，并且整个扇区必须位于 `0x00000000..0x00FFFFFF` 范围内。该参数是字节地址，不是扇区编号。

**返回值**

- `EXTERNAL_FLASH_RESULT_OK`：扇区擦除成功。
- `EXTERNAL_FLASH_RESULT_INVALID_ARGUMENT`：`sector_address` 未按 4 KiB 对齐。
- `EXTERNAL_FLASH_RESULT_NOT_INITIALIZED`：驱动尚未初始化。
- `EXTERNAL_FLASH_RESULT_OUT_OF_RANGE`：目标扇区超出有效地址范围。
- `EXTERNAL_FLASH_RESULT_BUSY`：驱动所使用的通信资源当前忙。
- `EXTERNAL_FLASH_RESULT_TIMEOUT`：等待操作完成超时。
- `EXTERNAL_FLASH_RESULT_IO_ERROR`：操作期间发生通信错误。
