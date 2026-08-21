#!/usr/bin/env python3
"""每秒读取两个 Modbus TCP 输入寄存器。

安装依赖：
    python -m pip install "pymodbus>=3.11,<4"

运行：
    python read_modbus_input.py
"""

import sys
import time
from datetime import datetime, timedelta, timezone

try:
    from pymodbus.client import ModbusTcpClient
    from pymodbus.exceptions import ModbusException
except ImportError:
    print(
        '缺少 pymodbus，请先执行：python -m pip install "pymodbus>=3.11,<4"',
        file=sys.stderr,
    )
    raise SystemExit(1) from None


HOST = "192.168.5.123"
PORT = 502
DEVICE_ID = 0x01
START_ADDRESS = 0x00
REGISTER_COUNT = 2
POLL_INTERVAL_SECONDS = 1.0
TIMEOUT_SECONDS = 3.0
BEIJING_TIMEZONE = timezone(timedelta(hours=8), name="北京时间")


def create_client() -> ModbusTcpClient:
    """创建尚未连接的同步 Modbus TCP 客户端。"""
    return ModbusTcpClient(HOST, port=PORT, timeout=TIMEOUT_SECONDS, retries=0)


def read_input_values(client: ModbusTcpClient) -> list[int]:
    """读取并校验两个 U16 输入寄存器。"""
    response = client.read_input_registers(
        START_ADDRESS,
        count=REGISTER_COUNT,
        device_id=DEVICE_ID,
    )

    if response.isError():
        raise ModbusException(f"从机返回 Modbus 异常响应：{response}")

    registers = getattr(response, "registers", None)
    if registers is None or len(registers) < REGISTER_COUNT:
        actual_count = 0 if registers is None else len(registers)
        raise ModbusException(
            f"响应寄存器数量不足：期望 {REGISTER_COUNT}，实际 {actual_count}"
        )

    values = [int(value) for value in registers[:REGISTER_COUNT]]
    if any(value < 0 or value > 0xFFFF for value in values):
        raise ModbusException(f"响应包含非 U16 数值：{values}")

    return values


def format_output(values: list[int]) -> str:
    """使用北京时间格式化寄存器读取结果。"""
    timestamp = datetime.now(BEIJING_TIMEZONE).strftime("%Y-%m-%d %H:%M:%S")
    return f"[{timestamp} 北京时间] input[0]={values[0]}, input[1]={values[1]}"


def close_client(client: ModbusTcpClient | None) -> None:
    """关闭客户端；清理连接时不掩盖原始通信错误。"""
    if client is None:
        return

    try:
        client.close()
    except Exception as exc:  # 关闭失败不应阻止后续重连或退出。
        print(f"关闭连接失败：{exc}", file=sys.stderr, flush=True)


def main() -> int:
    client: ModbusTcpClient | None = None

    try:
        while True:
            try:
                if client is None:
                    client = create_client()
                    if not client.connect():
                        raise ConnectionError(f"无法连接到 {HOST}:{PORT}")

                values = read_input_values(client)
                print(format_output(values), flush=True)
            except (ModbusException, OSError, ConnectionError) as exc:
                print(f"通信错误：{exc}", file=sys.stderr, flush=True)
                close_client(client)
                client = None

            time.sleep(POLL_INTERVAL_SECONDS)
    except KeyboardInterrupt:
        print("\n已停止。", file=sys.stderr, flush=True)
        return 0
    finally:
        close_client(client)


if __name__ == "__main__":
    raise SystemExit(main())
