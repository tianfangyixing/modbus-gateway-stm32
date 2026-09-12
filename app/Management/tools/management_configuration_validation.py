#!/usr/bin/env python3
"""Prepare v2 maximum wire vectors, or explicitly PUT/restart/GET a dedicated test device.

Input payloads must be produced/validated by the production Configuration codec
test fixtures. This script checks schema/length and transports bytes; it is not
a configuration editor or a substitute for model/certificate validation.
Without --write-and-restart, no serial port is opened.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import time

from management_status_monitor import (
    ManagementFrameParser, encode_management_frame, _load_pyserial,
    MAX_FRAME_LENGTH, MAX_PAYLOAD_LENGTH, MANAGEMENT_USB_VID, MANAGEMENT_USB_PID,
)

CONFIGURATION_SCHEMA_VERSION = 2
CONFIGURATION_MAX_PAYLOAD_LENGTH = MAX_PAYLOAD_LENGTH - 2
CONFIGURATION_DEFAULT_PAYLOAD_LENGTH = 48


def read_fixture(path: Path, length: int) -> bytes:
    payload = path.read_bytes()
    if len(payload) != length or payload[0] != CONFIGURATION_SCHEMA_VERSION:
        raise ValueError(f"{path}: expected schema 2 and exactly {length} bytes")
    return payload


def prepare_vectors(maximum: bytes, default: bytes) -> dict[str, bytes]:
    if len(maximum) != CONFIGURATION_MAX_PAYLOAD_LENGTH or maximum[0] != 2:
        raise ValueError("Maximum fixture must be schema 2 and exactly 8475 bytes")
    if len(default) != CONFIGURATION_DEFAULT_PAYLOAD_LENGTH or default[0] != 2:
        raise ValueError("Default fixture must be schema 2 and exactly 48 bytes")
    return {
        "maximum_put.bin": encode_management_frame(2, 0x72000001, maximum),
        "maximum_get_response.bin": encode_management_frame(0x81, 0x72000002, b"\0\0" + maximum),
        "invalid_8476_put.bin": encode_management_frame(2, 0x72000003, maximum + b"\0"),
        "v1_default_put.bin": encode_management_frame(2, 0x72000004, b"\x01" + default[1:]),
    }


def exchange(cdc, parser, message_type, transaction_id, payload, timeout, expected_result=0):
    request = encode_management_frame(message_type, transaction_id, payload)
    if cdc.write(request) != len(request):
        raise RuntimeError("Short serial write; transaction state is uncertain")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        frames = parser.feed(cdc.read(max(1, min(cdc.in_waiting, 4096))))
        if not frames:
            continue
        if len(frames) != 1:
            raise RuntimeError("More than one response to a single pending transaction")
        frame = frames[0]
        if frame.message_type != (message_type | 0x80) or frame.transaction_id != transaction_id:
            raise RuntimeError("Unexpected response type/transaction; stop before sending another request")
        if len(frame.payload) < 2 or struct.unpack_from("<H", frame.payload)[0] != expected_result:
            raise RuntimeError(f"Unexpected result for command 0x{message_type:02x}")
        if (message_type != 1 or expected_result != 0) and len(frame.payload) != 2:
            raise RuntimeError("Unexpected response data")
        if message_type == 1 and expected_result == 0:
            configuration = frame.payload[2:]
            if not configuration or configuration[0] != 2 or len(configuration) > CONFIGURATION_MAX_PAYLOAD_LENGTH:
                raise RuntimeError("GET returned an unsupported or oversized configuration")
        return frame
    raise TimeoutError("No complete matching response; do not retry PUT automatically")


def checked_identity(list_ports, port, previous=None):
    matches = [item for item in list_ports.comports() if item.device.upper() == port.upper()]
    if len(matches) != 1:
        raise RuntimeError(f"Management port {port} is not present")
    item = matches[0]
    if (item.vid, item.pid) != (MANAGEMENT_USB_VID, MANAGEMENT_USB_PID):
        raise RuntimeError("Selected port does not match the Management USB VID/PID")
    identity = {"device": item.device, "vid": item.vid, "pid": item.pid, "serial_number": item.serial_number}
    if previous and previous["serial_number"] and item.serial_number != previous["serial_number"]:
        raise RuntimeError("USB serial identity changed after restart")
    return identity


def run_device(args, maximum, default, record):
    serial, list_ports = _load_pyserial()
    identity = checked_identity(list_ports, args.port)
    record["usb_identity"] = identity

    def save_case(name, frame):
        record["cases"].append({
            "name": name, "result_code": struct.unpack_from("<H", frame.payload)[0],
            "frame_length": len(frame.raw), "payload_length": len(frame.payload),
            "frame_sha256": hashlib.sha256(frame.raw).hexdigest(),
        })
        (args.output / "device_result.json").write_text(json.dumps(record, indent=2), encoding="utf-8")

    with serial.Serial(args.port, baudrate=115200, timeout=0.02, write_timeout=args.timeout) as cdc:
        parser = ManagementFrameParser()
        initial = exchange(cdc, parser, 1, 0x73000001, b"", args.timeout)
        save_case("initial_active", initial)
        invalid = exchange(cdc, parser, 2, 0x73000002, maximum + b"\0", args.timeout, 3)
        save_case("8476_put_rejected", invalid)
        v1 = exchange(cdc, parser, 2, 0x73000003, b"\x01" + default[1:], args.timeout, 3)
        save_case("v1_put_rejected", v1)
        written = exchange(cdc, parser, 2, 0x73000004, maximum, args.timeout)
        save_case("maximum_put_persisted", written)
        active = exchange(cdc, parser, 1, 0x73000005, b"", args.timeout)
        if active.payload != initial.payload:
            raise AssertionError("Active configuration changed before restart")
        save_case("active_unchanged_before_restart", active)
        restarted = exchange(cdc, parser, 4, 0x73000006, b"", args.timeout)
        save_case("restart_acknowledged", restarted)

    # Require actual USB disappearance; a successful GET on the old session does
    # not establish that reboot happened (especially if the fixture was already active).
    deadline = time.monotonic() + args.reconnect_timeout
    disconnected = False
    while time.monotonic() < deadline:
        present = any(item.device.upper() == args.port.upper() for item in list_ports.comports())
        if not present:
            disconnected = True
        if disconnected and present:
            break
        time.sleep(0.1)
    else:
        raise TimeoutError("Did not observe USB disconnect/reconnect after RESTART")
    checked_identity(list_ports, args.port, identity)
    with serial.Serial(args.port, baudrate=115200, timeout=0.02, write_timeout=args.timeout) as cdc:
        active = exchange(cdc, ManagementFrameParser(), 1, 0x73000007, b"", args.timeout)
        if active.payload != b"\0\0" + maximum or len(active.raw) != MAX_FRAME_LENGTH:
            raise AssertionError("Post-restart maximum GET bytes/length differ from the input fixture")
        save_case("rebooted_maximum_get", active)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", type=Path, required=True, help="Validated maximum_v2.bin (8475 bytes)")
    parser.add_argument("--default-payload", type=Path, required=True, help="Validated default_v2.bin (48 bytes)")
    parser.add_argument("--output", type=Path, required=True, help="New output directory")
    parser.add_argument("--port", help="Explicit dedicated test-device COM port")
    parser.add_argument("--write-and-restart", action="store_true", help="Persist fixture and restart the device")
    parser.add_argument("--timeout", type=float, default=30.0, help="Per-command timeout including Flash erase/readback")
    parser.add_argument("--reconnect-timeout", type=float, default=30.0)
    args = parser.parse_args()
    if args.write_and_restart and not args.port:
        parser.error("--write-and-restart requires --port")
    if args.port and not args.write_and_restart:
        parser.error("--port requires explicit --write-and-restart; otherwise generate offline vectors")
    if args.timeout <= 0 or args.reconnect_timeout <= 0:
        parser.error("Timeouts must be positive")
    maximum = read_fixture(args.payload, CONFIGURATION_MAX_PAYLOAD_LENGTH)
    default = read_fixture(args.default_payload, CONFIGURATION_DEFAULT_PAYLOAD_LENGTH)
    vectors = prepare_vectors(maximum, default)
    args.output.mkdir(parents=True, exist_ok=False)
    metadata = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "configuration_schema": 2, "configuration_max_payload_length": CONFIGURATION_MAX_PAYLOAD_LENGTH,
        "management_max_payload_length": MAX_PAYLOAD_LENGTH, "management_max_frame_length": MAX_FRAME_LENGTH,
        "maximum_put_frame_length": len(vectors["maximum_put.bin"]),
        "input_sha256": hashlib.sha256(maximum).hexdigest(),
        "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "timeout_seconds": args.timeout,
        "hardware_requested": args.write_and_restart,
        "fixture_validation": "Schema/length only here; model validity must come from the production codec",
        "vectors": {name: {"length": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                    for name, data in vectors.items()},
    }
    for name, data in vectors.items():
        (args.output / name).write_bytes(data)
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    if not args.write_and_restart:
        print("Offline vectors prepared. No serial port opened; no device write/restart performed.")
        return 0
    record = {"complete": False, "cases": [], "error": None}
    try:
        run_device(args, maximum, default, record)
        record["complete"] = True
    except Exception as error:
        record["error"] = str(error)
    (args.output / "device_result.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    print(json.dumps(record, indent=2))
    return 0 if record["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
