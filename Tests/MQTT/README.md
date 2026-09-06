# MQTT Publisher 派生 Client ID 测试

`mqtt_publisher_client_id_tests` 以 `MQTT_PUBLISHER_CLIENT_ID_TEST` 模式编译
`MQTT/src/mqtt_publisher.c`。该模式只编译 Publisher 内部的纯 UID 编码逻辑和测试钩子，不链接
FreeRTOS、LwIP MQTT client、TLS 或 STM32 HAL 实现，也不形成生产公开 API。编译真实的看门狗公共头时，
仅使用 `support/management/` 提供的主机 RTOS 类型声明。

## 规范条款映射

| 规范条款 | 覆盖用例 |
| --- | --- |
| UID 按 w0/w1/w2 big-endian 串接，RFC 4648 大写 Base32、无 padding、`STM` 前缀 | `test_client_id_fixed_vectors` |
| 全零、全一、最高位、最低位和跨 word 固定向量保留全部 96 bit | `test_client_id_fixed_vectors` |
| 结果固定为 23 个 ASCII 字母数字字符并以 NUL 结束，结果确定 | `test_client_id_is_alphanumeric_and_deterministic` |

测试仅验证 Publisher 内部派生算法。MQTT 字段不再经过运行配置映射层；生产代码直接读取
`configuration_service_active()` 提供的稳定 active MQTT 配置。
