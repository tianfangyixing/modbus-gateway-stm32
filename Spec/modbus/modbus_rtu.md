# Modbus RTU 函数 API 目标规范

> 状态：目标契约（normative）
> 适用文件：`include/modbus_rtu.h`、`src/modbus_rtu.c`

本文档以函数为阐述对象，规定 `modbus_rtu.h` 当前公共函数和通道回调的
外部契约。每个函数条目依次给出函数原型、函数行为、参数说明和返回值说明。
调用要求、成功保证、执行流程及其他补充约束统一写入“函数行为”。

本文档只覆盖头文件中实际生效的声明。未声明的请求解码器、写功能编解码器和
`modbus_rtu_get_type()` 均不是本规范定义的公共 API。当本文档与其他说明材料
对相同 API 的描述冲突时，以本文档为准。

## 1. 阅读约定

### 1.1 函数条目结构

每个函数或回调按照以下顺序说明：

1. 函数原型；
2. 函数行为，包括调用要求、执行过程、成功保证和其他必要约束；
3. 每一个参数的用途、方向和约束；
4. 每一个返回值或返回范围的含义。

“必须”“不得”和“仅当”均为规范性要求。“未规定”表示调用方不得依赖具体
行为。

### 1.2 有效 RTU 包

本文档中的“有效 RTU 包”必须同时满足：

- `length` 在 `4..MODBUS_RTU_MAX_LENGTH` 范围内；
- 从站地址在 `1..247` 范围内，广播地址 `0` 不受支持；
- 功能码适合当前函数和帧类型；
- 长度、数量、地址范围、字节数、数据值和填充位符合相应功能定义；
- CRC 覆盖 CRC 字节之前的所有内容，并与帧尾 CRC 一致。

CRC 使用初值 `0xFFFF` 和多项式 `0xA001`，按最低有效位优先计算；帧尾先放
CRC 低字节，再放 CRC 高字节。起始地址、数量和寄存器值采用大端字节序。
线圈和离散输入从第一个数据字节的最低有效位开始排列，最后一个数据字节中
未使用的高位必须为零。

仅 `data[0]` 至 `data[length - 1]` 属于 RTU 包。`data[length]` 之后的内容
未规定。

### 1.3 通用成功和失败规则

- 所有编码函数仅在返回 `MODBUS_RTU_OK` 时保证输出 `rtu` 是有效 RTU 包；
- 所有解码函数仅在返回 `MODBUS_RTU_OK` 时保证输入 `response` 是该函数对应
  的有效 RTU 包，并保证输出参数已经全部写入；
- 编码或解码失败时，所有输出内容均未规定，调用方不得读取、发送或转发；
- 同一输入同时违反多项规则时，函数可以返回任一适用错误，调用方不得依赖
  检查顺序；
- “有效”只表示协议结构、数据取值和请求对应关系正确，不保证设备报告的值
  与真实物理状态一致。

## 2. 公共类型速查

### 2.1 常量

| 常量 | 值 | 含义 |
| --- | ---: | --- |
| `MODBUS_RTU_MAX_LENGTH` | 256 | 完整 RTU 帧最大长度 |
| `MODBUS_READ_COILS_MAX_COILS` | 2000 | 读线圈最大数量 |
| `MODBUS_READ_DISCRETE_INPUTS_MAX_DISCRETE_INPUTS` | 2000 | 读离散输入最大数量 |
| `MODBUS_READ_HOLDING_REGISTERS_MAX_REGISTERS` | 125 | 读保持寄存器最大数量 |
| `MODBUS_READ_INPUT_REGISTERS_MAX_REGISTERS` | 125 | 读输入寄存器最大数量 |

### 2.2 `modbus_rtu_adu_t`

~~~c
typedef struct
{
    uint16_t length;
    uint8_t data[MODBUS_RTU_MAX_LENGTH];
} modbus_rtu_adu_t;
~~~

`length` 包含地址、功能码、功能数据和两个 CRC 字节。对象及其缓冲区始终由
调用方拥有，模块不保存指针，也不分配动态内存。

### 2.3 `modbus_rtu_result_t`

| 枚举值 | 含义 |
| --- | --- |
| `MODBUS_RTU_OK` | 编码或解码成功 |
| `MODBUS_RTU_INVALID_ARGUMENT` | 必需指针为空等参数错误 |
| `MODBUS_RTU_LENGTH_INVALID` | 总长度或功能专用长度错误 |
| `MODBUS_RTU_SLAVE_ADDRESS_INVALID` | 从站地址不在 `1..247` |
| `MODBUS_RTU_CRC_INVALID` | CRC 不匹配 |
| `MODBUS_RTU_FUNCTION_UNSUPPORTED` | 功能码不受当前校验或编码接口支持 |
| `MODBUS_RTU_FUNCTION_DOMAIN_INVALID` | 普通或异常功能码所在域错误 |
| `MODBUS_RTU_FUNCTION_MISMATCH` | 功能码与专用解码函数不匹配 |
| `MODBUS_RTU_ADDRESS_RANGE_INVALID` | 起始地址和数量超出 `0xFFFF` 地址空间 |
| `MODBUS_RTU_BYTE_COUNT_INVALID` | 字节数与数量或数据类型不一致 |
| `MODBUS_RTU_VALUE_INVALID` | 填充位、异常码或其他数据值非法 |
| `MODBUS_RTU_QUANTITY_INVALID` | 数量为零或超过功能上限 |

### 2.4 `modbus_rtu_channel_t`

~~~c
typedef struct
{
    void *context;
    modbus_rtu_read_fn read;
    modbus_rtu_write_fn write;
    uint32_t baud_rate;
} modbus_rtu_channel_t;
~~~

`context`、两个回调和 `baud_rate` 由 `modbus_rtu_channel_init()` 设置。调用方
不得绕过初始化函数直接拼装或修改该结构。两个回调的完整契约见第 4 节。

### 2.5 `modbus_rtu_channel_result_t`

| 枚举值 | 含义 |
| --- | --- |
| `MODBUS_RTU_CHANNEL_OK` | 通道初始化成功 |
| `MODBUS_RTU_CHANNEL_INVALID_ARGUMENT` | 通道指针或任一必需回调为空 |
| `MODBUS_RTU_CHANNEL_BAUD_RATE_INVALID` | 波特率为零 |

### 2.6 `modbus_rtu_transaction_result_t`

| 枚举值 | 含义 |
| --- | --- |
| `MODBUS_RTU_TRANSACTION_OK` | 有效且与请求对应的正常响应 |
| `MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE` | 有效且与请求对应的异常响应 |
| `MODBUS_RTU_TRANSACTION_INVALID_ARGUMENT` | 基本调用参数错误 |
| `MODBUS_RTU_TRANSACTION_ADAPTER_IO_ERROR` | 适配器错误或回调返回值越界 |
| `MODBUS_RTU_TRANSACTION_RESPONSE_TIMEOUT` | 等待完整响应帧超时 |
| `MODBUS_RTU_TRANSACTION_RESPONSE_LENGTH_INVALID` | 响应长度错误 |
| `MODBUS_RTU_TRANSACTION_RESPONSE_CRC_INVALID` | 响应 CRC 错误 |
| `MODBUS_RTU_TRANSACTION_RESPONSE_SLAVE_ADDRESS_MISMATCH` | 响应与请求从站地址不同 |
| `MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_MISMATCH` | 响应功能码不对应当前请求 |
| `MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_INVALID` | 响应功能码不受支持 |
| `MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH` | 响应数据与请求不对应 |
| `MODBUS_RTU_TRANSACTION_RESPONSE_DATA_INVALID` | 响应自身数据非法 |

## 3. 请求校验与编解码函数

### 3.1 `modbus_rtu_validate_request`

~~~c
modbus_rtu_result_t modbus_rtu_validate_request(const modbus_rtu_adu_t *rtu);
~~~

**函数行为**

校验一个普通 Modbus RTU 请求。本函数不修改 `rtu`，也不保存其中的指针。

支持的请求功能码为 `0x01`、`0x02`、`0x03`、`0x04`、`0x05`、`0x06`、
`0x0F` 和 `0x10`。返回 `MODBUS_RTU_OK` 时，`rtu` 保证是 CRC 正确、从站地址
位于 `1..247` 且满足下列功能专用约束的有效普通请求：

| 功能码 | 长度与数据约束 |
| ---: | --- |
| `0x01` | 固定 8 字节；数量 `1..2000`；地址范围不溢出 |
| `0x02` | 固定 8 字节；数量 `1..2000`；地址范围不溢出 |
| `0x03` | 固定 8 字节；数量 `1..125`；地址范围不溢出 |
| `0x04` | 固定 8 字节；数量 `1..125`；地址范围不溢出 |
| `0x05` | 固定 8 字节；线圈值只能是 `0x0000` 或 `0xFF00` |
| `0x06` | 固定 8 字节；寄存器值可以是任意 `uint16_t` |
| `0x0F` | 总长度为 `9 + byte_count`；数量 `1..1968`；字节数为 `ceil(quantity / 8)`；填充位为零；地址范围不溢出 |
| `0x10` | 总长度为 `9 + byte_count`；数量 `1..123`；字节数为 `quantity * 2`；地址范围不溢出 |

校验失败时，调用方不得把 `rtu` 作为有效请求发送或转发。同一输入同时违反
多项规则时，函数可以返回任一适用错误，调用方不得依赖检查顺序。

**参数**

- **`rtu`**：包含 CRC 的完整候选普通请求；不得为空。

**返回值**

- **`MODBUS_RTU_OK`**：请求完整且有效。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：`rtu` 为空。
- **`MODBUS_RTU_LENGTH_INVALID`**：总长度超出允许范围、不是功能规定的固定
  长度，或者不等于多写请求声明的字节数加固定开销。
- **`MODBUS_RTU_CRC_INVALID`**：CRC 不匹配。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：从站地址不在 `1..247`。
- **`MODBUS_RTU_FUNCTION_UNSUPPORTED`**：功能码不是本函数支持的普通请求功能码。
- **`MODBUS_RTU_ADDRESS_RANGE_INVALID`**：起始地址和数量超出 `0xFFFF` 地址空间。
- **`MODBUS_RTU_BYTE_COUNT_INVALID`**：多写请求的字节数与数量不一致。
- **`MODBUS_RTU_VALUE_INVALID`**：单写线圈值非法，或者多写线圈的填充位不为零。
- **`MODBUS_RTU_QUANTITY_INVALID`**：数量为零或超过相应功能上限。

### 3.2 `modbus_rtu_read_coils_encode_request`

~~~c
modbus_rtu_result_t modbus_rtu_read_coils_encode_request(
    modbus_rtu_adu_t *rtu,
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t quantity);
~~~

**函数行为**

编码功能码 `0x01` 的读线圈请求，并计算 CRC。成功帧固定为 8 字节。

返回 `MODBUS_RTU_OK` 时，`rtu` 是功能码 `0x01`、起始地址和数量均与参数
一致、CRC 正确的有效 8 字节 RTU 请求。

**参数**

- **`rtu`**：接收完整 RTU 请求；不得为空。
- **`slave_address`**：从站地址，必须位于 `1..247`。
- **`start_address`**：第一个线圈的零基协议地址。
- **`quantity`**：读取数量，必须位于
  `1..MODBUS_READ_COILS_MAX_COILS`。

**返回值**

- **`MODBUS_RTU_OK`**：编码成功。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：`rtu` 为空。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：从站地址非法。
- **`MODBUS_RTU_QUANTITY_INVALID`**：数量非法。
- **`MODBUS_RTU_ADDRESS_RANGE_INVALID`**：末地址超过 `0xFFFF`。

### 3.3 `modbus_rtu_read_coils_decode_response`

~~~c
modbus_rtu_result_t modbus_rtu_read_coils_decode_response(
    const modbus_rtu_adu_t *response,
    uint8_t *slave_address,
    uint16_t expected_quantity,
    bool *coil_values);
~~~

**函数行为**

校验并解码功能码 `0x01` 的正常响应。

`coil_values` 的可写容量必须至少为 `expected_quantity`；本函数没有容量参数，
无法检查该条件。输出存储不得与 `response` 重叠。

返回 `MODBUS_RTU_OK` 时，`response` 是有效的 `0x01` 正常响应，字节数与
`expected_quantity` 一致；`coil_values[0]` 对应第一个数据字节的 bit 0，
前 `expected_quantity` 个元素均已写入。

**参数**

- **`response`**：包含 CRC 的完整候选响应；不得为空。
- **`slave_address`**：成功时接收响应从站地址；不得为空。
- **`expected_quantity`**：原请求的线圈数量，必须位于
  `1..MODBUS_READ_COILS_MAX_COILS`。
- **`coil_values`**：成功时接收线圈值；不得为空，且必须至少包含
  `expected_quantity` 个 `bool` 元素。

**返回值**

- **`MODBUS_RTU_OK`**：响应有效并已完整解码。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：任一必需指针为空。
- **`MODBUS_RTU_QUANTITY_INVALID`**：期望数量非法。
- **`MODBUS_RTU_LENGTH_INVALID`**：总长度或声明长度非法。
- **`MODBUS_RTU_CRC_INVALID`**：CRC 错误。
- **`MODBUS_RTU_FUNCTION_MISMATCH`**：功能码不是 `0x01`。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：响应地址非法。
- **`MODBUS_RTU_BYTE_COUNT_INVALID`**：字节数不等于
  `ceil(expected_quantity / 8)`。
- **`MODBUS_RTU_VALUE_INVALID`**：末字节未使用的高位不为零。

### 3.4 `modbus_rtu_read_discrete_inputs_encode_request`

~~~c
modbus_rtu_result_t modbus_rtu_read_discrete_inputs_encode_request(
    modbus_rtu_adu_t *rtu,
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t quantity);
~~~

**函数行为**

编码功能码 `0x02` 的读离散输入请求，并计算 CRC。成功帧固定为 8 字节。

返回 `MODBUS_RTU_OK` 时，`rtu` 是功能码 `0x02`、起始地址和数量均与参数
一致、CRC 正确的有效 8 字节 RTU 请求。

**参数**

- **`rtu`**：接收完整 RTU 请求；不得为空。
- **`slave_address`**：从站地址，必须位于 `1..247`。
- **`start_address`**：第一个离散输入的零基协议地址。
- **`quantity`**：读取数量，必须位于
  `1..MODBUS_READ_DISCRETE_INPUTS_MAX_DISCRETE_INPUTS`。

**返回值**

- **`MODBUS_RTU_OK`**：编码成功。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：`rtu` 为空。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：从站地址非法。
- **`MODBUS_RTU_QUANTITY_INVALID`**：数量非法。
- **`MODBUS_RTU_ADDRESS_RANGE_INVALID`**：末地址超过 `0xFFFF`。

### 3.5 `modbus_rtu_read_discrete_inputs_decode_response`

~~~c
modbus_rtu_result_t modbus_rtu_read_discrete_inputs_decode_response(
    const modbus_rtu_adu_t *response,
    uint8_t *slave_address,
    uint16_t expected_quantity,
    bool *input_values);
~~~

**函数行为**

校验并解码功能码 `0x02` 的正常响应。

`input_values` 的可写容量必须至少为 `expected_quantity`；本函数没有容量参数，
无法检查该条件。输出存储不得与 `response` 重叠。

返回 `MODBUS_RTU_OK` 时，`response` 是有效的 `0x02` 正常响应，字节数与
`expected_quantity` 一致；`input_values[0]` 对应第一个数据字节的 bit 0，
前 `expected_quantity` 个元素均已写入。

**参数**

- **`response`**：包含 CRC 的完整候选响应；不得为空。
- **`slave_address`**：成功时接收响应从站地址；不得为空。
- **`expected_quantity`**：原请求的离散输入数量，必须位于
  `1..MODBUS_READ_DISCRETE_INPUTS_MAX_DISCRETE_INPUTS`。
- **`input_values`**：成功时接收离散输入值；不得为空，且必须至少
  包含 `expected_quantity` 个 `bool` 元素。

**返回值**

- **`MODBUS_RTU_OK`**：响应有效并已完整解码。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：任一必需指针为空。
- **`MODBUS_RTU_QUANTITY_INVALID`**：期望数量非法。
- **`MODBUS_RTU_LENGTH_INVALID`**：总长度或声明长度非法。
- **`MODBUS_RTU_CRC_INVALID`**：CRC 错误。
- **`MODBUS_RTU_FUNCTION_MISMATCH`**：功能码不是 `0x02`。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：响应地址非法。
- **`MODBUS_RTU_BYTE_COUNT_INVALID`**：字节数不等于
  `ceil(expected_quantity / 8)`。
- **`MODBUS_RTU_VALUE_INVALID`**：末字节未使用的高位不为零。

### 3.6 `modbus_rtu_read_holding_registers_encode_request`

~~~c
modbus_rtu_result_t modbus_rtu_read_holding_registers_encode_request(
    modbus_rtu_adu_t *rtu,
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t quantity);
~~~

**函数行为**

编码功能码 `0x03` 的读保持寄存器请求，并计算 CRC。成功帧固定为 8 字节。

返回 `MODBUS_RTU_OK` 时，`rtu` 是功能码 `0x03`、起始地址和数量均与参数
一致、CRC 正确的有效 8 字节 RTU 请求。

**参数**

- **`rtu`**：接收完整 RTU 请求；不得为空。
- **`slave_address`**：从站地址，必须位于 `1..247`。
- **`start_address`**：第一个保持寄存器的零基协议地址。
- **`quantity`**：读取数量，必须位于
  `1..MODBUS_READ_HOLDING_REGISTERS_MAX_REGISTERS`。

**返回值**

- **`MODBUS_RTU_OK`**：编码成功。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：`rtu` 为空。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：从站地址非法。
- **`MODBUS_RTU_QUANTITY_INVALID`**：数量非法。
- **`MODBUS_RTU_ADDRESS_RANGE_INVALID`**：末地址超过 `0xFFFF`。

### 3.7 `modbus_rtu_read_holding_registers_decode_response`

~~~c
modbus_rtu_result_t modbus_rtu_read_holding_registers_decode_response(
    const modbus_rtu_adu_t *response,
    uint8_t *slave_address,
    uint16_t expected_quantity,
    uint16_t *register_values);
~~~

**函数行为**

校验并解码功能码 `0x03` 的正常响应。

`register_values` 的可写容量必须至少为 `expected_quantity`；本函数没有容量
参数，无法检查该条件。输出存储不得与 `response` 重叠。

返回 `MODBUS_RTU_OK` 时，`response` 是有效的 `0x03` 正常响应，前
`expected_quantity` 个寄存器均已按大端字节序解码到 `register_values`。

**参数**

- **`response`**：包含 CRC 的完整候选响应；不得为空。
- **`slave_address`**：成功时接收响应从站地址；不得为空。
- **`expected_quantity`**：原请求的保持寄存器数量，必须位于
  `1..MODBUS_READ_HOLDING_REGISTERS_MAX_REGISTERS`。
- **`register_values`**：成功时接收寄存器值；不得为空，且必须至少
  包含 `expected_quantity` 个 `uint16_t` 元素。

**返回值**

- **`MODBUS_RTU_OK`**：响应有效并已完整解码。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：任一必需指针为空。
- **`MODBUS_RTU_QUANTITY_INVALID`**：期望数量非法。
- **`MODBUS_RTU_LENGTH_INVALID`**：总长度或声明长度非法。
- **`MODBUS_RTU_CRC_INVALID`**：CRC 错误。
- **`MODBUS_RTU_FUNCTION_MISMATCH`**：功能码不是 `0x03`。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：响应地址非法。
- **`MODBUS_RTU_BYTE_COUNT_INVALID`**：字节数不是偶数或不等于
  `expected_quantity * 2`。

### 3.8 `modbus_rtu_read_input_registers_encode_request`

~~~c
modbus_rtu_result_t modbus_rtu_read_input_registers_encode_request(
    modbus_rtu_adu_t *rtu,
    uint8_t slave_address,
    uint16_t start_address,
    uint16_t quantity);
~~~

**函数行为**

编码功能码 `0x04` 的读输入寄存器请求，并计算 CRC。成功帧固定为 8 字节。

返回 `MODBUS_RTU_OK` 时，`rtu` 是功能码 `0x04`、起始地址和数量均与参数
一致、CRC 正确的有效 8 字节 RTU 请求。

**参数**

- **`rtu`**：接收完整 RTU 请求；不得为空。
- **`slave_address`**：从站地址，必须位于 `1..247`。
- **`start_address`**：第一个输入寄存器的零基协议地址。
- **`quantity`**：读取数量，必须位于
  `1..MODBUS_READ_INPUT_REGISTERS_MAX_REGISTERS`。

**返回值**

- **`MODBUS_RTU_OK`**：编码成功。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：`rtu` 为空。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：从站地址非法。
- **`MODBUS_RTU_QUANTITY_INVALID`**：数量非法。
- **`MODBUS_RTU_ADDRESS_RANGE_INVALID`**：末地址超过 `0xFFFF`。

### 3.9 `modbus_rtu_read_input_registers_decode_response`

~~~c
modbus_rtu_result_t modbus_rtu_read_input_registers_decode_response(
    const modbus_rtu_adu_t *response,
    uint8_t *slave_address,
    uint16_t expected_quantity,
    uint16_t *register_values);
~~~

**函数行为**

校验并解码功能码 `0x04` 的正常响应。

`register_values` 的可写容量必须至少为 `expected_quantity`；本函数没有容量
参数，无法检查该条件。输出存储不得与 `response` 重叠。

返回 `MODBUS_RTU_OK` 时，`response` 是有效的 `0x04` 正常响应，前
`expected_quantity` 个寄存器均已按大端字节序解码到 `register_values`。

**参数**

- **`response`**：包含 CRC 的完整候选响应；不得为空。
- **`slave_address`**：成功时接收响应从站地址；不得为空。
- **`expected_quantity`**：原请求的输入寄存器数量，必须位于
  `1..MODBUS_READ_INPUT_REGISTERS_MAX_REGISTERS`。
- **`register_values`**：成功时接收寄存器值；不得为空，且必须至少
  包含 `expected_quantity` 个 `uint16_t` 元素。

**返回值**

- **`MODBUS_RTU_OK`**：响应有效并已完整解码。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：任一必需指针为空。
- **`MODBUS_RTU_QUANTITY_INVALID`**：期望数量非法。
- **`MODBUS_RTU_LENGTH_INVALID`**：总长度或声明长度非法。
- **`MODBUS_RTU_CRC_INVALID`**：CRC 错误。
- **`MODBUS_RTU_FUNCTION_MISMATCH`**：功能码不是 `0x04`。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：响应地址非法。
- **`MODBUS_RTU_BYTE_COUNT_INVALID`**：字节数不是偶数或不等于
  `expected_quantity * 2`。

### 3.10 `modbus_rtu_encode_exception_response`

~~~c
modbus_rtu_result_t modbus_rtu_encode_exception_response(
    modbus_rtu_adu_t *rtu,
    uint8_t slave_address,
    uint8_t request_function,
    uint8_t exception_code);
~~~

**函数行为**

编码固定 5 字节的异常响应。线上功能码为 `request_function | 0x80`。

返回 `MODBUS_RTU_OK` 时，`rtu` 是地址正确、异常功能码正确、异常码有效且
CRC 正确的 5 字节 RTU 异常响应。

**参数**

- **`rtu`**：接收完整异常响应；不得为空。
- **`slave_address`**：从站地址，必须位于 `1..247`。
- **`request_function`**：原普通请求功能码，必须位于
  `0x01..0x7F`，不得预先设置最高位。
- **`exception_code`**：异常码，只允许
  `0x01/0x02/0x03/0x04/0x05/0x06/0x08/0x0A/0x0B`。

**返回值**

- **`MODBUS_RTU_OK`**：编码成功。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：`rtu` 为空。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：从站地址非法。
- **`MODBUS_RTU_FUNCTION_UNSUPPORTED`**：原请求功能码不在
  `0x01..0x7F`。
- **`MODBUS_RTU_VALUE_INVALID`**：异常码不在允许集合中。

### 3.11 `modbus_rtu_decode_exception_response`

~~~c
modbus_rtu_result_t modbus_rtu_decode_exception_response(
    const modbus_rtu_adu_t *response,
    uint8_t *slave_address,
    uint8_t *request_function,
    uint8_t *exception_code);
~~~

**函数行为**

校验并解码固定 5 字节的异常响应，输出去掉最高位后的原请求功能码。

返回 `MODBUS_RTU_OK` 时，`response` 是有效异常响应；三个输出参数均已写入，
其中 `request_function` 不包含异常标志位。

**参数**

- **`response`**：包含 CRC 的完整候选异常响应；不得为空。
- **`slave_address`**：成功时接收从站地址；不得为空。
- **`request_function`**：成功时接收 `0x01..0x7F` 范围内的原请求
  功能码；不得为空。
- **`exception_code`**：成功时接收标准异常码；不得为空。

**返回值**

- **`MODBUS_RTU_OK`**：异常响应有效并已完整解码。
- **`MODBUS_RTU_INVALID_ARGUMENT`**：任一必需指针为空。
- **`MODBUS_RTU_LENGTH_INVALID`**：响应长度不是 5。
- **`MODBUS_RTU_CRC_INVALID`**：CRC 错误。
- **`MODBUS_RTU_SLAVE_ADDRESS_INVALID`**：响应地址非法。
- **`MODBUS_RTU_FUNCTION_DOMAIN_INVALID`**：线上功能码不在
  `0x81..0xFF`。
- **`MODBUS_RTU_VALUE_INVALID`**：异常码不在允许集合中。

## 4. 通道回调

### 4.1 T3.5 共用定义

读写回调必须使用与 `channel->baud_rate` 相同的实际波特率。本 API 按每个
RTU 字符 11 位计算 T3.5：

~~~text
baud_rate <= 19200:
    T3.5_us = ceil(3.5 * 11 * 1,000,000 / baud_rate)

baud_rate > 19200:
    T3.5_us = 1,750
~~~

该分段规则来自
[MODBUS Serial Line Protocol and Implementation Guide V1.02](https://www.modbus.org/docs/Modbus_over_serial_line_V1_02.pdf)。

同一物理通道上的回调只能由一个执行上下文串行调用。成功的 `write` 后必须立即
调用一次 `read`，不得并发、重入或用其他顺序独立调用两个回调。

通道工作在单主站总线上。调用方和现场设备必须保证发送前恢复窗口内没有其他
设备发送数据；超过响应超时及平台恢复窗口后才到达的迟到响应违反通道运行
前提，平台不保证识别或等待该响应。

### 4.2 `modbus_rtu_read_fn`（`read` 回调）

~~~c
typedef int32_t (*modbus_rtu_read_fn)(
    void *context,
    uint8_t *buffer,
    uint16_t capacity,
    uint32_t timeout_ms);
~~~

**函数行为**

等待一个候选 RTU 帧，并用末字节后的 T3.5 静默判定帧结束。一次调用最多
返回一个候选帧。

`buffer` 必须指向至少 `capacity` 字节的可写存储。实际串口配置必须与通道
波特率和 11 位字符假定一致。

`timeout_ms` 约束从调用 `read` 开始到候选帧结束的全部等待时间，语义上覆盖
首字节响应时间与后续帧传输时间。由于回调使用末字节后的 T3.5 静默确认帧
结束，T3.5 分帧等待也必须在该期限内完成。

达到期限时，即使已经收到部分字节，回调也必须终止并丢弃当前未完成的候选
帧，返回 `0`；不得在返回后继续向 `buffer` 写入，也不得让这部分数据进入下一
次读取。

- 只要在期限内检测到 T3.5 边界，即使候选帧过短、CRC 错误或内容不完整，
  也必须返回实际收到的正数字节数；
- 不得把 T3.5 之后下一帧的字节合并到本次结果；
- 候选帧超过 `capacity` 时，不得把截断数据伪装成完整帧；必须丢弃到下一次
  T3.5 边界，并在该边界早于期限时返回 `-1`；若期限先到则返回 `0`；
- 回调只负责分帧和报告接收字节数，不负责 CRC 或功能数据校验。

**参数**

- **`context`**：平台适配上下文，即通道初始化时保存的指针。
- **`buffer`**：接收候选帧；回调最多写入 `capacity` 个字节。
- **`capacity`**：`buffer` 的字节容量。事务函数传入
  `MODBUS_RTU_MAX_LENGTH`。
- **`timeout_ms`**：等待完整候选帧的总超时毫秒数，覆盖首字节响应时间、帧
  传输时间以及确认帧结束所需的 T3.5 静默。

**返回值**

- **`-1`**：发生不可恢复的接收错误，`buffer` 内容不得使用。
- **`0`**：在 `timeout_ms` 内没有完成一个候选帧，包括未收到首字节或只收到
  部分帧。
- **`1..capacity`**：实际写入 `buffer` 的候选帧字节数。

小于 `-1` 或大于 `capacity` 的返回值违反回调契约。

### 4.3 `modbus_rtu_write_fn`（`write` 回调）

~~~c
typedef int32_t (*modbus_rtu_write_fn)(
    void *context,
    const uint8_t *buffer,
    uint16_t count,
    uint32_t timeout_ms);
~~~

**函数行为**

在发送前同步丢弃接收路径中的旧数据、等待一个至少为 T3.5 的有限恢复窗口，
然后把一个 RTU 帧作为连续字符流写入物理串行接口。

`buffer` 必须指向至少 `count` 字节的可读存储。实际串口配置必须与通道波特率
和 11 位字符假定一致。

- 发送首字节前，平台软件缓冲区以及可读取的 UART/DMA 接收缓存中的旧字节
  必须全部清除，并且不得在下一次 `read` 中出现；
- 恢复窗口开始和结束时均可再次清理接收路径；窗口内出现的新总线流量违反
  单主站运行前提，回调无需为该流量无限延长等待；
- 本帧必须连续发送，不得人为插入会形成新帧边界的静默间隔；
- 返回 `count` 时必须已经收到物理串行接口的发送完成事件，最后一个字节已经
  物理发送完成；
- 无法完整发送时必须中止当前发送并返回 `-1`，不得报告部分发送字节数。

- 发送前恢复窗口不受 `timeout_ms` 限制；
- 发送超时、适配器忙和串口或 DMA 错误均返回 `-1`；
- 回调不负责在末字节后额外等待 T3.5。

**参数**

- **`context`**：平台适配上下文，即通道初始化时保存的指针。
- **`buffer`**：待发送字节数组；回调只可读取前 `count` 个字节。
- **`count`**：需要发送的字节数。
- **`timeout_ms`**：只约束从启动发送到物理发送完成的过程，不包含发送前
  恢复窗口。

**返回值**

- **`-1`**：未能完整发送，已发送字节数不再可用。
- **`count`**：全部 `count` 个字节已经物理发送完成。

除 `-1` 和 `count` 外的返回值违反回调契约。

## 5. 通道函数

### 5.1 `modbus_rtu_channel_init`

~~~c
modbus_rtu_channel_result_t modbus_rtu_channel_init(
    modbus_rtu_channel_t *channel,
    void *context,
    modbus_rtu_read_fn read,
    modbus_rtu_write_fn write,
    uint32_t baud_rate);
~~~

**函数行为**

使用平台上下文、两个回调和波特率初始化同步事务通道。该函数只保存参数，
不得调用回调或访问硬件。

返回 `MODBUS_RTU_CHANNEL_OK` 时，`channel` 已保存全部参数，可以传给
`modbus_rtu_transact()`。失败时 `channel` 内容未规定。

成功初始化后，调用方必须保证回调和 `context` 的生命周期覆盖全部事务，且
不得直接修改 `channel` 字段。同一物理通道上的事务必须由上层串行化。

**参数**

- **`channel`**：接收初始化后的通道；不得为空。
- **`context`**：原样传给 `read`、`write` 的平台上下文；可以为空，但两个
  回调必须能够安全处理空上下文。
- **`read`**：读取回调；不得为空，必须满足第 4.2 节契约。
- **`write`**：写入回调；不得为空，必须满足第 4.3 节契约。
- **`baud_rate`**：实际 RTU 串行波特率；必须大于零。

**返回值**

- **`MODBUS_RTU_CHANNEL_OK`**：初始化成功。
- **`MODBUS_RTU_CHANNEL_INVALID_ARGUMENT`**：`channel` 或任一回调为空。
- **`MODBUS_RTU_CHANNEL_BAUD_RATE_INVALID`**：`baud_rate` 为零。

## 6. 事务函数

### 6.1 `modbus_rtu_transact`

~~~c
modbus_rtu_transaction_result_t modbus_rtu_transact(
    modbus_rtu_channel_t *channel,
    const modbus_rtu_adu_t *request,
    uint32_t response_timeout_ms,
    modbus_rtu_adu_t *response);
~~~

**函数行为**

在已初始化通道上同步执行一次 RTU 请求/响应事务。函数调用一次 `write`，并且
仅在完整写入后调用一次 `read`；函数不重试，也不拼接多次短读。

调用方必须确保 `request` 是 CRC 正确、从站地址位于 `1..247` 的有效普通
RTU 请求，并且功能码和数据满足下表。函数不会检查请求长度、CRC、地址、
功能码、数量、地址范围、字节数或数据值是否合理。

| 功能码 | 调用方必须保证的请求数据 |
| ---: | --- |
| `0x01` | 固定 8 字节；数量 `1..2000`；地址范围不溢出 |
| `0x02` | 固定 8 字节；数量 `1..2000`；地址范围不溢出 |
| `0x03` | 固定 8 字节；数量 `1..125`；地址范围不溢出 |
| `0x04` | 固定 8 字节；数量 `1..125`；地址范围不溢出 |
| `0x05` | 固定 8 字节；线圈值仅为 `0x0000` 或 `0xFF00` |
| `0x06` | 固定 8 字节；寄存器值可为任意 `uint16_t` |
| `0x0F` | 数量 `1..1968`；字节数为 `ceil(quantity / 8)`；填充位为零；地址范围不溢出 |
| `0x10` | 数量 `1..123`；字节数为 `quantity * 2`；地址范围不溢出 |

无效请求违反调用契约；该情况下返回值和 `response` 内容均未规定。

- `request` 与 `response` 必须是不同对象；
- `channel` 必须来自成功的 `modbus_rtu_channel_init()`；
- 调用期间不得有其他执行上下文使用同一物理通道。

函数按以下顺序执行：

1. 检查基本调用参数；
2. 计算写超时并调用一次 `write()`；
3. 仅在 `write()` 返回 `request->length` 时调用一次 `read()`；
4. 校验响应并返回事务结果。

写超时为：

~~~text
write_timeout_ms = ceil(request->length * 11 * 1000 / channel->baud_rate) + 100
~~~

该值包含物理发送时间和 100 ms 调度余量，不包含 `write` 在发送首字节前保证
T3.5 所用的时间。

回调返回值映射为：

| 回调结果 | 事务行为 |
| --- | --- |
| `write != request->length` | 返回适配器 I/O 错误，不读取响应 |
| `write == request->length` | 继续调用 `read` |
| `read == -1`、`read < -1` 或 `read > MODBUS_RTU_MAX_LENGTH` | 返回适配器 I/O 错误 |
| `read == 0` | 返回响应超时 |
| `1 <= read <= MODBUS_RTU_MAX_LENGTH` | 设置响应实际长度并继续校验 |

仅当返回 `MODBUS_RTU_TRANSACTION_OK` 时，`response` 保证是完整、CRC 正确、
从站地址与请求一致、功能码与请求一致且功能数据有效的正常响应，并满足：

| 请求功能码 | 响应对应关系 |
| ---: | --- |
| `0x01`、`0x02` | 字节数等于 `ceil(request_quantity / 8)`，填充位为零 |
| `0x03`、`0x04` | 字节数为偶数且等于 `request_quantity * 2` |
| `0x05`、`0x06` | 固定 8 字节，原样回显请求地址和值 |
| `0x0F`、`0x10` | 固定 8 字节，回显请求起始地址和数量 |

该结果不保证设备真实执行了请求，也不保证读回值反映真实物理状态。

仅当返回 `MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE` 时，`response` 保证：

- 长度固定为 5 字节；
- CRC 正确；
- 从站地址等于请求地址；
- 功能码等于 `request_function | 0x80`；
- 异常码仅为
  `0x01/0x02/0x03/0x04/0x05/0x06/0x08/0x0A/0x0B`。

该结果不判断从站选择该异常码的业务原因是否真实。

返回上述两个成功结果以外的任何值时，`response` 可能包含旧数据、部分帧或
未经验证的原始数据；其 `length` 和 `data` 均未规定，调用方不得解析、使用
或转发。

**参数**

- **`channel`**：成功初始化且未被修改的通道；不得为空。
- **`request`**：调用方已经完整校验的数据有效请求；不得为空。
- **`response_timeout_ms`**：等待完整响应帧的总超时毫秒数，覆盖首字节响应
  时间、帧传输时间和 T3.5 分帧等待；必须大于零，并原样传给 `read`。
- **`response`**：接收一次 `read` 返回的候选响应；不得为空，并且
  必须与 `request` 指向不同对象。

**返回值**

- **`MODBUS_RTU_TRANSACTION_OK`**：收到有效且与请求对应的正常响应。
- **`MODBUS_RTU_TRANSACTION_EXCEPTION_RESPONSE`**：收到有效且与请求
  对应的标准异常响应。
- **`MODBUS_RTU_TRANSACTION_INVALID_ARGUMENT`**：基本指针参数为空或
  `response_timeout_ms` 为零。
- **`MODBUS_RTU_TRANSACTION_ADAPTER_IO_ERROR`**：写回调未返回完整请求长度，
  或读回调返回 `-1` 或超出合法范围。
- **`MODBUS_RTU_TRANSACTION_RESPONSE_TIMEOUT`**：读回调未在总超时时间内
  完成响应帧并返回 `0`。
- **`MODBUS_RTU_TRANSACTION_RESPONSE_LENGTH_INVALID`**：响应长度不符合
  功能规定或字节数声明。
- **`MODBUS_RTU_TRANSACTION_RESPONSE_CRC_INVALID`**：响应 CRC 错误。
- **`MODBUS_RTU_TRANSACTION_RESPONSE_SLAVE_ADDRESS_MISMATCH`**：响应
  地址与请求地址不同。
- **`MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_MISMATCH`**：响应功能码
  合法，但不对应当前请求。
- **`MODBUS_RTU_TRANSACTION_RESPONSE_FUNCTION_INVALID`**：响应功能码
  不是受支持的正常功能码或对应异常功能码。
- **`MODBUS_RTU_TRANSACTION_RESPONSE_DATA_MISMATCH`**：响应数据形状
  有效，但数量、地址或回显值与请求不一致。
- **`MODBUS_RTU_TRANSACTION_RESPONSE_DATA_INVALID`**：响应自身数据
  非法，例如奇数字节数、非零填充位或非法异常码。
