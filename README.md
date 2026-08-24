# Modbus网关
基于STM32平台，支持Modbus RTU与Modbus TCP协议转换，并可按配置采集数据并上报至MQTT服务器。目前处于开发阶段。


## 问题
1. 由于Cubemx配置`MBEDTLS_SSL_OUT_CONTENT_LEN`，`MBEDTLS_SSL_IN_CONTENT_LEN`不生效。所以在`lwipopts.h`手动配置
2. 在mbedtls_hardware_poll中，HAL_RNG_GenerateRandomNumber不返回HAL_OK直接调用Error_Handler
3. stm32cubemx启用mbedtls，和在lwip启用mbedtls，会生成重复文件