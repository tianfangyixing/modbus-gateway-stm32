#!/usr/bin/env python3
"""Poll input registers 0x0000 and 0x0001 on unit 0x01 every five seconds.

Usage:
    python Modbus/tools/modbus_tcp_input_monitor.py --host 192.168.1.100 --port 502

Requires only Python 3. Connects immediately, prints each register as uint16 in
the order 0x0001, 0x0000, and reconnects after communication failures. Ctrl+C exits.
Addresses are zero-based Modbus PDU addresses, not 3xxxx register numbers.
"""

import argparse
from datetime import datetime
import ipaddress
import socket
import struct
import sys
import time


UNIT_ID = 0x01
READ_INPUT_REGISTERS = 0x04
INTERVAL_SECONDS = 5.0
TIMEOUT_SECONDS = 3.0


class ModbusError(Exception):
    """Invalid Modbus response or server exception response."""


def receive_exact(connection, size, deadline):
    """Read a complete field even when TCP splits it across multiple packets."""
    data = bytearray()
    while len(data) < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Modbus response timed out")
        connection.settimeout(remaining)
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise ConnectionError("Server closed the connection")
        data.extend(chunk)
    return bytes(data)


def read_input_registers(connection, transaction_id):
    # MBAP: transaction, protocol, length (unit + PDU), unit.
    # FC04: start at address 0x0000 and read two consecutive registers.
    request = struct.pack(
        ">HHHBBHH", transaction_id, 0, 6, UNIT_ID, READ_INPUT_REGISTERS, 0x0000, 2
    )
    deadline = time.monotonic() + TIMEOUT_SECONDS
    connection.settimeout(TIMEOUT_SECONDS)
    connection.sendall(request)
    header = receive_exact(connection, 7, deadline)
    response_id, protocol_id, length, unit_id = struct.unpack(">HHHB", header)
    if response_id != transaction_id or protocol_id != 0 or unit_id != UNIT_ID:
        raise ModbusError("Response transaction, protocol or unit ID does not match")
    if not 2 <= length <= 254:
        raise ModbusError(f"Invalid MBAP length: {length}")

    pdu = receive_exact(connection, length - 1, deadline)
    if pdu[0] == (READ_INPUT_REGISTERS | 0x80) and len(pdu) == 2:
        raise ModbusError(f"Server returned Modbus exception 0x{pdu[1]:02X}")
    if len(pdu) != 6 or pdu[0] != READ_INPUT_REGISTERS or pdu[1] != 4:
        raise ModbusError(f"Invalid FC04 response: {pdu.hex(' ')}")
    # Modbus register bytes are big-endian; H decodes unsigned 16-bit values.
    return struct.unpack(">HH", pdu[2:])


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Modbus TCP server IP address")
    parser.add_argument("--port", type=int, default=502, help="TCP port (default: 502)")
    args = parser.parse_args()
    try:
        ipaddress.ip_address(args.host)
    except ValueError:
        parser.error("--host must be a valid IP address")
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    return args


def main():
    args = parse_arguments()
    connection = None
    transaction_id = 0
    next_poll = time.monotonic()
    print(f"Server {args.host}:{args.port}, unit=0x01, interval=5s. Ctrl+C to stop.", flush=True)
    try:
        while True:
            time.sleep(max(0.0, next_poll - time.monotonic()))
            try:
                if connection is None:
                    connection = socket.create_connection((args.host, args.port), TIMEOUT_SECONDS)
                transaction_id = (transaction_id + 1) & 0xFFFF
                value_0000, value_0001 = read_input_registers(connection, transaction_id)
                timestamp = datetime.now().isoformat(sep=" ", timespec="seconds")
                print(f"[{timestamp}] 0x0001={value_0001}  0x0000={value_0000}", flush=True)
            except (OSError, ModbusError) as error:
                timestamp = datetime.now().isoformat(sep=" ", timespec="seconds")
                print(f"[{timestamp}] Error: {error}; retry on next poll.", file=sys.stderr, flush=True)
                if connection is not None:
                    connection.close()
                    connection = None

            # Keep start-to-start timing; skip missed slots instead of bursting.
            next_poll += INTERVAL_SECONDS
            now = time.monotonic()
            if next_poll < now:
                missed = int((now - next_poll) // INTERVAL_SECONDS) + 1
                next_poll += missed * INTERVAL_SECONDS
    except KeyboardInterrupt:
        print("\nStopped.", flush=True)
    finally:
        if connection is not None:
            connection.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
