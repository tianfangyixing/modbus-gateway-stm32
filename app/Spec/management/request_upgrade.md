# 升级请求命令 REQUEST_UPGRADE

## 模块来源

属于网关的 **Management 管理模块**，通过 USB CDC 传输。
命令处理位于 `Management/src/management_transport.c`，帧编解码位于 `Management/src/management_frame.c`。

## 请求帧格式

总长度 **17 字节**，所有多字节整数均为小端序。

| 字段 | 长度 | 内容 |
|---|---|---|
| magic | 4 字节 | `4D 42 47 57`，即 ASCII `MBGW` |
| message_type | 1 字节 | `0x05`（REQUEST_UPGRADE） |
| transaction_id | 4 字节 | 主机生成的事务 ID |
| payload_length | 4 字节 | `0` |
| payload | 0 字节 | 空 |
| crc32 | 4 字节 | CRC-32/ISO-HDLC，覆盖此前全部字节 |

## 响应帧格式

总长度 **19 字节**，所有多字节整数均为小端序。

| 字段 | 长度 | 内容 |
|---|---|---|
| magic | 4 字节 | `4D 42 47 57`，即 ASCII `MBGW` |
| message_type | 1 字节 | `0x85`（REQUEST_UPGRADE_RESPONSE） |
| transaction_id | 4 字节 | 与请求一致 |
| payload_length | 4 字节 | `2` |
| result_code | 2 字节 | `0`：成功；`1`：请求 payload 非空；`7`：备份域写访问或寄存器写入 / 回读校验失败 |
| crc32 | 4 字节 | CRC-32/ISO-HDLC，覆盖此前全部字节 |

## 作用

将 RTC 备份寄存器 `RTC->BKP0R`（`RTC_BKP_DR0`，地址 `0x40002850`）写为 `1` 并回读校验，
作为下次复位进入升级模式的标志。

命令成功后设备继续运行，不自动重启；上位机可另发 `RESTART (0x04)`。
外部 Bootloader 负责在复位后识别标志、进入升级模式，并在 USB 初始化成功后将标志清为 `0`。
本命令不传输或烧写固件。
