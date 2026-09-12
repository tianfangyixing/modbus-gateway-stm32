#!/usr/bin/env python3
"""Request and print one Management GET_STATUS response per Enter press over USB CDC.

Examples:
    python Management/tools/management_status_monitor.py --list
    python Management/tools/management_status_monitor.py
    python Management/tools/management_status_monitor.py --port COM7

The reported response length is the complete Management Frame length, including
the 13-byte header and 4-byte CRC. Every request, including the first request and
timeout retries, requires pressing Enter. Press Ctrl+C to stop.
"""

from __future__ import annotations

import argparse
import secrets
import signal
import struct
import sys
import threading
import time
import zlib
from dataclasses import dataclass
from datetime import datetime
from typing import Any, Sequence


MAGIC = b"MBGW"
HEADER_LENGTH = 13
CRC_LENGTH = 4
MAX_PAYLOAD_LENGTH = 8192
MAX_FRAME_LENGTH = HEADER_LENGTH + MAX_PAYLOAD_LENGTH + CRC_LENGTH

GET_STATUS = 0x03
GET_STATUS_RESPONSE = 0x83
ERROR_RESPONSE = 0xFF
STATUS_RESPONSE_PAYLOAD_LENGTH = 17

MANAGEMENT_USB_VID = 0x0483
MANAGEMENT_USB_PID = 0x5740
MANAGEMENT_USB_PRODUCT = "Modbus Gateway Management Port"


class PortSelectionError(RuntimeError):
    """Raised when the Management USB CDC port cannot be selected safely."""


@dataclass(frozen=True)
class ManagementFrame:
    message_type: int
    transaction_id: int
    payload: bytes
    raw: bytes


class ManagementFrameParser:
    """Streaming parser that does not assume USB reads are frame boundaries."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def reset(self) -> None:
        self._buffer.clear()

    def feed(self, data: bytes) -> list[ManagementFrame]:
        self._buffer.extend(data)
        frames: list[ManagementFrame] = []

        while True:
            magic_index = self._buffer.find(MAGIC)
            if magic_index < 0:
                self._keep_possible_magic_prefix()
                return frames

            if magic_index > 0:
                del self._buffer[:magic_index]

            if len(self._buffer) < HEADER_LENGTH:
                return frames

            payload_length = struct.unpack_from("<I", self._buffer, 9)[0]
            if payload_length > MAX_PAYLOAD_LENGTH:
                del self._buffer[0]
                continue

            frame_length = HEADER_LENGTH + payload_length + CRC_LENGTH
            if len(self._buffer) < frame_length:
                return frames

            crc_offset = HEADER_LENGTH + payload_length
            expected_crc = struct.unpack_from("<I", self._buffer, crc_offset)[0]
            actual_crc = zlib.crc32(self._buffer[:crc_offset]) & 0xFFFFFFFF
            if actual_crc != expected_crc:
                del self._buffer[0]
                continue

            raw = bytes(self._buffer[:frame_length])
            frames.append(
                ManagementFrame(
                    message_type=raw[4],
                    transaction_id=struct.unpack_from("<I", raw, 5)[0],
                    payload=raw[HEADER_LENGTH:crc_offset],
                    raw=raw,
                )
            )
            del self._buffer[:frame_length]

    def _keep_possible_magic_prefix(self) -> None:
        keep_length = min(len(self._buffer), len(MAGIC) - 1)
        while keep_length > 0:
            if self._buffer[-keep_length:] == MAGIC[:keep_length]:
                del self._buffer[:-keep_length]
                return
            keep_length -= 1
        self._buffer.clear()


def encode_management_frame(message_type: int, transaction_id: int, payload: bytes = b"") -> bytes:
    if not 0 <= message_type <= 0xFF:
        raise ValueError("message_type must be in the range 0..255")
    if not 0 <= transaction_id <= 0xFFFFFFFF:
        raise ValueError("transaction_id must be in the range 0..0xFFFFFFFF")
    if len(payload) > MAX_PAYLOAD_LENGTH:
        raise ValueError(f"payload must not exceed {MAX_PAYLOAD_LENGTH} bytes")

    frame_without_crc = MAGIC + bytes((message_type,)) + struct.pack("<II", transaction_id, len(payload)) + payload
    crc = zlib.crc32(frame_without_crc) & 0xFFFFFFFF
    return frame_without_crc + struct.pack("<I", crc)


def _load_pyserial() -> tuple[Any, Any]:
    try:
        import serial
        from serial.tools import list_ports
    except ImportError as exc:
        raise RuntimeError("pyserial is required; install it with: python -m pip install pyserial") from exc
    return serial, list_ports


def _port_text(port_info: Any) -> str:
    values = (
        getattr(port_info, "product", None),
        getattr(port_info, "description", None),
        getattr(port_info, "interface", None),
    )
    return " ".join(str(value) for value in values if value)


def _has_management_name(port_info: Any) -> bool:
    return MANAGEMENT_USB_PRODUCT.casefold() in _port_text(port_info).casefold()


def _has_management_usb_id(port_info: Any) -> bool:
    return (
        getattr(port_info, "vid", None) == MANAGEMENT_USB_VID
        and getattr(port_info, "pid", None) == MANAGEMENT_USB_PID
    )


def _format_port(port_info: Any) -> str:
    vid = getattr(port_info, "vid", None)
    pid = getattr(port_info, "pid", None)
    usb_id = f"{vid:04X}:{pid:04X}" if vid is not None and pid is not None else "----:----"
    description = getattr(port_info, "description", None) or "no description"
    return f"{port_info.device}  VID:PID={usb_id}  {description}"


def list_serial_ports(list_ports_module: Any) -> list[Any]:
    ports = sorted(list_ports_module.comports(), key=lambda item: item.device)
    if not ports:
        print("No serial ports found.")
        return ports

    print("Available serial ports:")
    for port_info in ports:
        marker = " [Management]" if _has_management_name(port_info) or _has_management_usb_id(port_info) else ""
        print(f"  {_format_port(port_info)}{marker}")
    return ports


def select_management_port(list_ports_module: Any) -> str:
    ports = list(list_ports_module.comports())
    named_matches = [port for port in ports if _has_management_name(port)]
    usb_id_matches = [port for port in ports if _has_management_usb_id(port)]
    strongest_matches = [port for port in named_matches if _has_management_usb_id(port)]

    candidates = strongest_matches or named_matches or usb_id_matches
    if len(candidates) == 1:
        return str(candidates[0].device)

    if not candidates:
        available = "\n".join(f"  {_format_port(port)}" for port in ports) or "  (none)"
        raise PortSelectionError(
            "Modbus Gateway Management USB CDC port was not found.\n"
            f"Expected product: {MANAGEMENT_USB_PRODUCT}\n"
            f"Expected VID:PID: {MANAGEMENT_USB_VID:04X}:{MANAGEMENT_USB_PID:04X}\n"
            f"Available ports:\n{available}\n"
            "Check the device connection or select it explicitly with --port COMx."
        )

    candidate_text = "\n".join(f"  {_format_port(port)}" for port in candidates)
    raise PortSelectionError(
        "Multiple Management USB CDC candidates were found:\n"
        f"{candidate_text}\n"
        "Select one explicitly with --port COMx."
    )


def _print_response(frame: ManagementFrame) -> None:
    timestamp = datetime.now().astimezone().isoformat(timespec="milliseconds")
    hex_packet = frame.raw.hex(" ").upper()
    print(f"[{timestamp}] response_length={len(frame.raw)} hex={hex_packet}", flush=True)


def _warn_if_abnormal_status_response(frame: ManagementFrame) -> None:
    if frame.message_type not in (GET_STATUS_RESPONSE, ERROR_RESPONSE):
        print(
            f"Warning: response type is 0x{frame.message_type:02X}; expected 0x{GET_STATUS_RESPONSE:02X}.",
            file=sys.stderr,
        )

    if len(frame.payload) < 2:
        print("Warning: response payload is shorter than the result_code field.", file=sys.stderr)
        return

    result_code = struct.unpack_from("<H", frame.payload)[0]
    if result_code != 0:
        print(f"Warning: device returned result_code={result_code}.", file=sys.stderr)
    elif frame.message_type == GET_STATUS_RESPONSE and len(frame.payload) != STATUS_RESPONSE_PAYLOAD_LENGTH:
        print(
            f"Warning: successful GET_STATUS payload length is {len(frame.payload)}; "
            f"expected {STATUS_RESPONSE_PAYLOAD_LENGTH}.",
            file=sys.stderr,
        )


def _read_matching_response(
    cdc: Any,
    frame_parser: ManagementFrameParser,
    transaction_id: int,
    response_timeout: float,
    stop_event: threading.Event,
) -> ManagementFrame | None:
    deadline = time.monotonic() + response_timeout

    while not stop_event.is_set() and time.monotonic() < deadline:
        waiting = int(cdc.in_waiting)
        chunk = cdc.read(min(max(waiting, 1), MAX_FRAME_LENGTH))
        if not chunk:
            continue

        for frame in frame_parser.feed(chunk):
            _print_response(frame)
            if frame.transaction_id == transaction_id:
                return frame
            print(
                f"Ignoring stale transaction_id=0x{frame.transaction_id:08X}; "
                f"waiting for 0x{transaction_id:08X}.",
                file=sys.stderr,
            )

    return None


def _wait_for_enter(stop_event: threading.Event) -> bool:
    prompt = "Press Enter to send GET_STATUS: "
    if sys.platform != "win32" or not sys.stdin.isatty():
        return input(prompt) == "" and not stop_event.is_set()

    import msvcrt

    # Poll console input so Python can handle Ctrl+C even when stdout is redirected.
    print(prompt, end="", flush=True)
    extended_key = False
    while not stop_event.is_set():
        if msvcrt.kbhit():
            character = msvcrt.getwch()
            if extended_key:
                extended_key = False
            elif character in ("\x00", "\xe0"):
                extended_key = True
            elif character in ("\r", "\n"):
                print(flush=True)
                return True
            elif character == "\x03":
                raise KeyboardInterrupt
        time.sleep(0.05)
    return False


def monitor_status(
    serial_module: Any,
    port: str,
    baud_rate: int,
    response_timeout: float,
    response_count: int,
    initial_transaction_id: int | None,
    stop_event: threading.Event,
) -> None:
    transaction_id = initial_transaction_id if initial_transaction_id is not None else secrets.randbits(32)
    received_count = 0
    frame_parser = ManagementFrameParser()

    with serial_module.serial_for_url(
        port,
        baudrate=baud_rate,
        timeout=min(response_timeout, 0.1),
        write_timeout=min(response_timeout, 0.5),
    ) as cdc:
        cdc.reset_input_buffer()
        print(
            f"Connected to {port} (line coding {baud_rate} bps). "
            "Each request requires Enter. Press Ctrl+C to stop.",
            flush=True,
        )

        while not stop_event.is_set() and (response_count == 0 or received_count < response_count):
            if not _wait_for_enter(stop_event):
                continue
            if stop_event.is_set():
                break

            request = encode_management_frame(GET_STATUS, transaction_id)
            written = cdc.write(request)
            if written != len(request):
                raise serial_module.SerialTimeoutException(f"wrote only {written}/{len(request)} bytes")
            if stop_event.is_set():
                break

            response = _read_matching_response(cdc, frame_parser, transaction_id, response_timeout, stop_event)
            if stop_event.is_set():
                break
            if response is None:
                frame_parser.reset()
                print(
                    f"Timed out waiting for transaction_id=0x{transaction_id:08X}; "
                    "press Enter to retry the same request.",
                    file=sys.stderr,
                    flush=True,
                )
            else:
                _warn_if_abnormal_status_response(response)
                received_count += 1
                transaction_id = (transaction_id + 1) & 0xFFFFFFFF


def _parse_u32(value: str) -> int:
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be decimal or hexadecimal with a 0x prefix") from exc
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("must be in the range 0..0xFFFFFFFF")
    return parsed


def _enable_windows_ctrl_c() -> None:
    if sys.platform != "win32":
        return

    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetConsoleCP.argtypes = []
    kernel32.GetConsoleCP.restype = wintypes.UINT
    if kernel32.GetConsoleCP() == 0:
        return

    # Windows can inherit an ignore-Ctrl+C flag independently of Python's SIGINT handler.
    # Restore delivery even when stdin is redirected; it need not be a console handle.
    kernel32.SetConsoleCtrlHandler.argtypes = [ctypes.c_void_p, wintypes.BOOL]
    kernel32.SetConsoleCtrlHandler.restype = wintypes.BOOL
    if not kernel32.SetConsoleCtrlHandler(None, False):
        raise ctypes.WinError(ctypes.get_last_error())


def _build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Press Enter to send each Management GET_STATUS over USB CDC and print its response frame."
    )
    parser.add_argument("--port", help="serial port, for example COM7 or /dev/ttyACM0; default: auto-detect")
    parser.add_argument("--baud", type=int, default=115200, help="CDC line coding baud rate (default: 115200)")
    parser.add_argument("--timeout", type=float, default=2.0, help="seconds to wait for a complete response (default: 2.0)")
    parser.add_argument("--count", type=int, default=0, help="stop after this many responses; 0 runs forever (default: 0)")
    parser.add_argument(
        "--transaction-id",
        type=_parse_u32,
        help="first transaction ID, decimal or hexadecimal with a 0x prefix (default: random)",
    )
    parser.add_argument("--list", action="store_true", help="list serial ports and exit")
    parser.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    return parser


def _self_test() -> None:
    expected_request = bytes.fromhex("4D 42 47 57 03 78 56 34 12 00 00 00 00 51 E4 C0 0E")
    request = encode_management_frame(GET_STATUS, 0x12345678)
    if request != expected_request:
        raise AssertionError("GET_STATUS encoding does not match the protocol example")

    damaged_request = bytearray(request)
    damaged_request[-1] ^= 0x01
    frame_parser = ManagementFrameParser()
    if frame_parser.feed(b"noise" + damaged_request + request[:7]):
        raise AssertionError("parser accepted a frame with an invalid CRC")
    frames = frame_parser.feed(request[7:] + request)
    if [frame.raw for frame in frames] != [request, request]:
        raise AssertionError("parser did not recover from noise, fragmentation, or coalescing")


def main(argv: Sequence[str] | None = None) -> int:
    argument_parser = _build_argument_parser()
    args = argument_parser.parse_args(argv)

    if args.baud <= 0:
        argument_parser.error("--baud must be greater than zero")
    if args.timeout <= 0:
        argument_parser.error("--timeout must be greater than zero")
    if args.count < 0:
        argument_parser.error("--count must not be negative")

    if args.self_test:
        _self_test()
        print("Self-test passed.")
        return 0

    try:
        serial_module, list_ports_module = _load_pyserial()
    except RuntimeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    stop_event = threading.Event()

    stop_signals = [signal.SIGINT]
    if hasattr(signal, "SIGBREAK"):
        stop_signals.append(signal.SIGBREAK)
    previous_signal_handlers = {stop_signal: signal.getsignal(stop_signal) for stop_signal in stop_signals}
    try:
        # Keep signal handlers free of Event locks; KeyboardInterrupt unwinds the serial context.
        for stop_signal in stop_signals:
            signal.signal(stop_signal, signal.default_int_handler)
        _enable_windows_ctrl_c()

        if args.list:
            list_serial_ports(list_ports_module)
            return 0

        port = args.port or select_management_port(list_ports_module)
        monitor_status(
            serial_module=serial_module,
            port=port,
            baud_rate=args.baud,
            response_timeout=args.timeout,
            response_count=args.count,
            initial_transaction_id=args.transaction_id,
            stop_event=stop_event,
        )
    except (KeyboardInterrupt, EOFError):
        stop_event.set()
    except PortSelectionError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    except serial_module.SerialException as exc:
        print(f"Serial error: {exc}", file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    finally:
        for stop_signal, previous_handler in previous_signal_handlers.items():
            signal.signal(stop_signal, previous_handler)

    if stop_event.is_set():
        print("\nStopped.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
