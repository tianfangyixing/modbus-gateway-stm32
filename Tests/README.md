# 主机黑盒测试

本目录只构建和运行主机测试，不替代 Keil 固件工程，也不向根目录增加固件 CMake 构建入口。

## 构建与运行

本机验证环境：

- GCC 15.2.0，目标为 `x86_64-w64-mingw32`
- CMake/CTest 4.3.2
- GNU Make 4.4.1

```powershell
cmake -S Tests -B Tests/build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=D:/mingw64/bin/gcc.exe
cmake --build Tests/build --parallel
ctest --test-dir Tests/build --output-on-failure
```

八个 CTest executable 分别为：

- `management_frame_tests`
- `management_transport_tests`
- `configuration_defaults_tests`
- `configuration_validate_tests`
- `configuration_equals_tests`
- `configuration_property_tests`
- `configuration_binary_codec_tests`
- `configuration_service_tests`

只运行固定种子属性测试：

```powershell
ctest --test-dir Tests/build --output-on-failure -L properties
```

属性测试默认纳入完整 `ctest`，超时为 120 秒。

## 黑盒边界

配置模型测试以 `Spec/configuration/configuration.md` 为唯一预期来源，只包含 `configuration.h` 并调用以下
三个公开 interface 函数：

- `configuration_set_defaults()`
- `configuration_validate()`
- `configuration_equals()`

测试不调用 `configuration.c` 的内部函数，也不依赖其校验顺序。每个确定性无效用例从有效基线只破坏一项
规则。`configuration_equals()` 只接收两个已经通过 `configuration_validate()` 的对象。

二进制 codec 测试以 `Spec/configuration/configuration_binary.md` 为唯一预期来源，只包含
`configuration_binary_codec.h` 并调用 `configuration_binary_decode()` 与 `configuration_binary_encode()`。
测试使用独立 golden payload 和规范字节拼装器，不调用 codec 内部 helper，也不使用生产 encoder 生成
decoder 的唯一判据。

持久化测试以 `Spec/configuration/configuration_service.md` 为预期来源，只通过
`configuration_service.h` 的三个函数观察初始化、active 与写入行为。测试链接内存 External Flash adapter，
并使用仅在主机测试构建中启用的 reset seam 模拟设备重启；槽解析、选槽与 CRC helper 均不对测试公开。

主机测试链接真实的：

- `Configuration/src/configuration.c`
- `Configuration/src/configuration_binary_codec.c`
- `Configuration/src/configuration_service.c`
- LwIP `def.c` 与 `ip4_addr.c`
- 固件配置所需的 mbedTLS 2.16.2 X.509、PEM、PK、摘要、RSA/ECDSA 和 platform 模块

`support/lwipopts.h` 仅隔离无关的 STM32 HAL/CMSIS 头；`support/mbedtls_platform_host.c` 仅提供生产
mbedTLS 配置要求的线程安全 UTC 时间转换。测试启动时为 mbedTLS 安装标准 `calloc/free`，资源不足用例
临时安装确定性失败 allocator，随后恢复。

`support/configuration_service_test_adapter.c` 提供 16 KiB 内存 Flash 和可控的读/擦/写故障注入。它模拟
Flash 的 `1 -> 0` 编程约束及部分编程失败，不进入固件工程；配置服务的 8 KiB 工作区由生产代码静态持有。

Management 测试直接链接 `Management/src/management_frame.c` 和 `Management/src/management_transport.c`。
`support/management/` 只替换 FreeRTOS tick/通知、USB CDC、Configuration、SNTP、MQTT、LwIP netif、日志与
系统复位这些硬件或调度边界。CRC、流式 parser、transaction/session、响应缓存和重试状态机均使用生产实现。
用例与摘要条款的映射见 [Management/README.md](Management/README.md)。

## Unity

测试固定使用 ThrowTheSwitch Unity `v2.7.0`，只随仓库保存核心三个源码文件和 MIT 许可证：

- `third_party/unity/unity.c`
- `third_party/unity/unity.h`
- `third_party/unity/unity_internals.h`
- `third_party/unity/LICENSE.txt`

来源：[Unity v2.7.0](https://github.com/ThrowTheSwitch/Unity/releases/tag/v2.7.0)。配置和构建过程不访问网络。

## 属性测试

每个属性组执行 10,000 次，固定种子如下：

| 属性组 | 种子 |
| --- | --- |
| 任意完整对象的安全、确定性和输入不变性 | `0x45A1D39B` |
| 有效对象单字节变异 | `0xA8C751E3` |
| 有效配置上的相等关系定律 | `0x19F04B6D` |
| 随机原内容上的默认值幂等性 | `0xD37A2C91` |

当前 MinGW 安装缺少 `libasan` 和 `libubsan`，因此本地测试不启用 ASan/UBSan。将来可在 Linux CI、Clang
或带相应 runtime 的 MSYS2 工具链上叠加 sanitizer；这不影响当前确定性与属性测试的预期。

## 证书夹具

`Configuration/fixtures/` 保存测试专用固定证书。测试运行时不生成证书，也不使用生产 MQTT 根证书。
根目录 `.gitattributes` 将 PEM/DER 标记为不进行文本归一化，确保 LF、CRLF 和 DER 字节在不同 Git
环境中保持不变。

| 文件 | 用途 |
| --- | --- |
| `valid_root.pem` | RSA/SHA-256 自签名根，包含 `keyCertSign` |
| `alternate_root.pem` | 第二张有效根，用于精确比较 |
| `valid_root_crlf.pem` | 与 `valid_root.pem` 同证书、不同 PEM 字节表示 |
| `valid_root_no_key_usage.pem` | 缺少 Key Usage 的有效根 |
| `expired_root.pem`、`future_root.pem` | 有效期不覆盖当前时间但仍应接受 |
| `valid_ecdsa_root.pem` | 当前固件支持的 ECDSA 根 |
| `not_ca.pem` | `CA = FALSE` |
| `intermediate_ca.pem`、`cross_signed_ca.pem` | Subject 与 Issuer 不同的 CA |
| `wrong_key_usage.pem` | Key Usage 不含 `keyCertSign` |
| `bad_self_signature.pem` | 自签名签名值被破坏 |
| `unsupported_algorithm.pem` | 当前 mbedTLS 不支持的 Ed25519 签名 |
| `bundle.pem` | 两张证书组成的 bundle |
| `malformed.pem` | 畸形 PEM |
| `valid_root.der` | 不允许的 DER 表示 |

重新生成夹具需要 Python 和 `cryptography`：

```powershell
python Tests/Configuration/fixtures/generate_certificates.py
```

生成脚本只把证书写入仓库，私钥仅存在于进程内存。密钥随机生成，因此重新生成会改变证书字节；提交的
输出才是测试使用的固定夹具。

## 失败策略

当用例失败时，保留失败用例和原始预期，不删除、不跳过、不放宽断言，也不修改生产实现。报告应给出失败
测试、实际值、预期值以及对应规范条款。测试基础设施自身的编译或路径错误可以在 `Tests/` 内修正。
