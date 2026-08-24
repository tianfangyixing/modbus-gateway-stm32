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
│   ├── Inc/                         # STM32、FreeRTOS、外设配置及应用层公共头文件
│   └── Src/                         # 系统启动、外设、中断、RTOS 任务及网关集成代码
├── Modbus/
│   ├── include/                     # Modbus 模块公共类型及 API
│   └── src/                         # RTU、RS485 端口、ADU 池、事务调度器和 TCP 服务器
├── MQTT/
│   ├── include/                     # MQTT 发布接口、根证书及 TLS 安全策略
│   └── src/                         # MQTT 连接发布、根证书和 LwIP ALTCP TLS 适配
├── SNTP/
│   ├── include/                     # SNTP 校时服务公共接口
│   └── src/                         # LwIP SNTP、RTC 写入及同步状态实现
├── Spec/
│   └── modbus/                      # Modbus 模块的 API 与行为规范
├── Scripts/
│   └── modbus_tcp_reader/           # 主机侧 Modbus TCP 轮询与联调工具
├── LWIP/
│   ├── App/                         # LwIP 初始化和应用层集成
│   └── Target/                      # 以太网接口及 LwIP 配置
├── MBEDTLS/
│   ├── App/                         # Mbed TLS 初始化、功能配置及网络接口适配
│   └── Target/                      # 硬件随机数与平台函数适配
├── USB_DEVICE/
│   ├── App/                         # USB Device 初始化、描述符及 CDC 接口
│   └── Target/                      # USB Device HAL 底层适配与配置
├── Drivers/
│   ├── CMSIS/                       # ARM CMSIS 与 STM32F4 设备支持
│   ├── STM32F4xx_HAL_Driver/        # STM32F4 HAL 驱动
│   └── BSP/Components/lan8742/      # LAN8742 以太网 PHY 驱动
├── Middlewares/
│   ├── ST/STM32_USB_Device_Library/ # ST USB Device 核心库及 CDC 类实现
│   └── Third_Party/
│       ├── FreeRTOS/                # FreeRTOS 内核及 CMSIS-RTOS2 适配层
│       ├── LwIP/                    # LwIP 协议栈、MQTT、SNTP 及 ALTCP TLS 源码
│       ├── mbedTLS/                 # Mbed TLS 头文件与密码学、TLS、X.509 源码
│       └── RTT/                     # SEGGER RTT 调试日志组件
├── MDK-ARM/                         # Keil MDK 工程、启动文件及调试配置
├── modbus-gateway-stm32.ioc         # STM32CubeMX 工程配置
├── .mxproject                       # STM32CubeMX 工程元数据
├── README.md                         # 项目简介
└── todo.txt                         # 待办事项
```

主要入口与修改边界：

- `Core/Src/main.c`：系统启动、时钟、RTC、RNG、Mbed TLS 和其他外设的初始化入口。
- `Core/Src/freertos.c`：RTOS 对象创建以及 USB Device、LwIP、SNTP、Modbus 网关和 MQTT 的初始化入口。
- `Core/Src/modbus_gateway_app.c`：组装并启动 RS485 端口、RTU 事务调度器与 Modbus TCP 服务器。
- `Modbus/`：项目自有的核心协议与网关业务代码；公共接口放在 `include/`，实现放在 `src/`。
- 修改 `Modbus/` 前应检查 `Spec/modbus/` 中对应规范，并保持接口、返回值和资源所有权约定一致。
- `MQTT/` 与 `SNTP/`：项目自有的网络应用模块；修改时应同步检查 LwIP 配置、网络就绪及 RTC 校时时序。
- `MBEDTLS/`：CubeMX 生成的 Mbed TLS 配置与平台适配；MQTT TLS 策略和根证书仍由 `MQTT/` 维护。
- `USB_DEVICE/` 由 CubeMX 生成，`Middlewares/ST/STM32_USB_Device_Library/` 为厂商中间件；USB CDC 同时被调试日志使用。
- `Drivers/` 和 `Middlewares/Third_Party/` 主要是厂商或第三方代码，除非确有必要，不应直接修改。
- `Core/`、`LWIP/`、`MBEDTLS/`、`USB_DEVICE/` 等 CubeMX 生成文件中的自定义代码，应尽量放在 `USER CODE BEGIN` 与 `USER CODE END` 区域内，避免重新生成工程时丢失。
- 硬件、引脚、时钟或外设配置变更应同步维护 `modbus-gateway-stm32.ioc`。

## C 代码规范

- 所有多行代码块、类型定义和初始化的大括号均采用 Allman 风格。
- C 代码应优先保持紧凑。函数调用、函数声明和条件表达式在缩进后不超过 120 个字符时，必须写在同一行，禁止无必要地按参数换行；仅当超过 120 个字符或参数本身是多行表达式时才允许换行。
- 禁止使用任何方式显式丢弃值，也不得因此删除对应的函数调用、参数或变量。

## 注意事项

