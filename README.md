# Modbus网关
基于STM32平台，支持Modbus RTU与Modbus TCP协议转换，并可按配置采集数据并上报至MQTT服务器。目前处于开发阶段。


## 问题
1. 由于Cubemx配置`MBEDTLS_SSL_OUT_CONTENT_LEN`，`MBEDTLS_SSL_IN_CONTENT_LEN`不生效。所以在`lwipopts.h`手动配置
2. 在mbedtls_hardware_poll中，HAL_RNG_GenerateRandomNumber不返回HAL_OK直接调用Error_Handler
3. stm32cubemx启用mbedtls，和在lwip启用mbedtls，会生成重复文件

## Management USB CDC

`Management/` 提供基于 USB CDC 字节流的设备管理通道，支持读取当前配置、持久化新配置、读取设备状态和
完成响应后重启。线路帧格式和 transaction/session 行为见
[Spec/management/management_frame_summary.md](Spec/management/management_frame_summary.md)。

## 主机黑盒测试

`Configuration` 的配置模型、持久化 interface 与 `Management` 协议实现可在 Windows 主机上通过
GCC、CMake 和 CTest 进行黑盒测试：

```powershell
cmake -S Tests -B Tests/build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=D:/mingw64/bin/gcc.exe
cmake --build Tests/build --parallel
ctest --test-dir Tests/build --output-on-failure
```

测试范围、依赖、固定种子和证书夹具说明见 [Tests/README.md](Tests/README.md)，规范条款与测试组的映射见
[Tests/Configuration/README.md](Tests/Configuration/README.md) 和
[Tests/Management/README.md](Tests/Management/README.md)。
