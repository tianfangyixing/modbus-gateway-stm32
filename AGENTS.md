# Modbus网关
该网关基于STM32F407ZGT6平台，支持Modbus RTU与Modbus TCP协议转换，并可按配置采集数据并上报至MQTT服务器。

## 使用库
1. FreeRTOS 10.3.1
2. LWIP 2.1.2
3. STM32F4 HAL V1.8.5

## 项目结构

```text
.
├── Core/
│   ├── Inc/                         # STM32、FreeRTOS 及外设配置头文件
│   └── Src/                         # 程序入口、外设初始化、中断和 RTOS 任务
├── Modbus/
│   ├── include/                     # Modbus 模块公共类型及 API
│   └── src/                         # RTU、ADU 池、事务调度器和 TCP 服务器实现
├── Spec/
│   └── modbus/                      # Modbus 模块的 API 与行为规范
├── LWIP/
│   ├── App/                         # LwIP 初始化和应用层集成
│   └── Target/                      # 以太网接口及 LwIP 配置
├── Drivers/
│   ├── CMSIS/                       # ARM CMSIS 与 STM32F4 设备支持
│   ├── STM32F4xx_HAL_Driver/        # STM32F4 HAL 驱动
│   └── BSP/Components/lan8742/      # LAN8742 以太网 PHY 驱动
├── Middlewares/
│   └── Third_Party/
│       ├── FreeRTOS/                # FreeRTOS 内核及 CMSIS-RTOS2 适配层
│       └── LwIP/                    # LwIP 协议栈源码
├── MDK-ARM/                         # Keil MDK 工程、启动文件及调试配置
├── modbus-gateway-stm32.ioc         # STM32CubeMX 工程配置
├── .mxproject                       # STM32CubeMX 工程元数据
└── todo.txt                         # 待办事项
```

主要入口与修改边界：

- `Core/Src/main.c`：系统启动、时钟和外设初始化入口。
- `Core/Src/freertos.c`：RTOS 对象创建、LwIP 初始化及应用任务入口。
- `Modbus/`：项目自有的核心协议与网关业务代码；公共接口放在 `include/`，实现放在 `src/`。
- 修改 `Modbus/` 前应检查 `Spec/modbus/` 中对应规范，并保持接口、返回值和资源所有权约定一致。
- `Drivers/` 和 `Middlewares/Third_Party/` 主要是厂商或第三方代码，除非确有必要，不应直接修改。
- `Core/`、`LWIP/` 等 CubeMX 生成文件中的自定义代码，应尽量放在 `USER CODE BEGIN` 与 `USER CODE END` 区域内，避免重新生成工程时丢失。
- 硬件、引脚、时钟或外设配置变更应同步维护 `modbus-gateway-stm32.ioc`。

## C 代码规范

- 所有多行代码块、类型定义和初始化的大括号均采用 Allman 风格。
- C 代码应优先保持紧凑。函数调用、函数声明和条件表达式在缩进后不超过 120 个字符时，必须写在同一行，禁止无必要地按参数换行；仅当超过 120 个字符或参数本身是多行表达式时才允许换行。
- 禁止使用 `(void)expr;` 显式丢弃值，也不得因此删除对应的函数调用、参数或变量。

## 注意事项

