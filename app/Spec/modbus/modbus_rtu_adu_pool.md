# Modbus RTU ADU Pool 函数 API 规范

> 状态：目标契约（normative）
> 适用文件：`include/modbus_rtu_adu_pool.h`、`src/modbus_rtu_adu_pool.c`

本文档以函数为阐述对象，规定 `modbus_rtu_adu_pool.h` 中公共函数的外部契约。
每个函数条目依次给出函数原型、函数行为、参数和返回值。

## 1. 模块概述

ADU 池使用模块内部的静态存储管理 `modbus_rtu_adu_t` 对象，不进行动态内存
分配。池容量由 `MODBUS_RTU_ADU_POOL_CAPACITY` 定义，当前为 16。

调用方必须先初始化池，再分配或释放 ADU。分配成功后，调用方独占返回的
ADU，直至将其释放回池。

## 2. API

### 2.1 `modbus_rtu_adu_pool_init`

~~~c
void modbus_rtu_adu_pool_init(void);
~~~

**函数行为**

初始化 ADU 池及其内部互斥量，将全部池槽标记为可分配，并将每个 ADU 的
`length` 置为 0。

该函数在整个程序生命周期内只能调用一次。该函数返回后，才能调用
`modbus_rtu_adu_pool_allocate()` 和 `modbus_rtu_adu_pool_release()`。

**参数**

无。

**返回值**

无。

### 2.2 `modbus_rtu_adu_pool_allocate`

~~~c
modbus_rtu_adu_t *modbus_rtu_adu_pool_allocate(void);
~~~

**函数行为**

从池中分配一个可用的 ADU。该函数只能在 `modbus_rtu_adu_pool_init()` 成功
返回后调用。

该函数线程安全，不得在 ISR 中调用。函数使用内部互斥量保护池状态；如果池中
没有可用 ADU，则返回 `NULL`，不会等待其他 ADU 被释放。

分配成功后，返回的 ADU 由调用方独占，直至调用方将其释放回池。返回 ADU 的
`length` 为 0；调用方不得依赖 `data` 中的原有内容。

**参数**

无。

**返回值**

- **非空指针**：分配成功，指向调用方独占的池内 ADU。
- **`NULL`**：池中没有可用 ADU。

### 2.3 `modbus_rtu_adu_pool_release`

~~~c
void modbus_rtu_adu_pool_release(modbus_rtu_adu_t *adu);
~~~

**函数行为**

将 ADU 释放回池。该函数只能在 `modbus_rtu_adu_pool_init()` 成功返回后调用。

该函数线程安全，不得在 ISR 中调用。释放时，函数将 `adu->length` 置为 0，
并将对应池槽标记为可分配；`data` 中的内容不会被清除。

调用方必须确认 `adu` 属于本池，并且尚未被释放。`adu` 必须是
`modbus_rtu_adu_pool_allocate()` 成功返回的准确地址，不得为空、不得指向池外
对象或 ADU 内部地址。函数不检查或报告这些调用错误；违反上述要求时，行为
未规定。

函数返回后，调用方不再拥有该 ADU，不得继续访问或再次释放它，除非后续重新
分配得到同一地址。

**参数**

- **`adu`**：待释放的池内 ADU；必须属于本池且当前尚未释放。

**返回值**

无。
