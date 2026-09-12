#!/usr/bin/env python3
"""Exercise USB Management framing using read-only GET_STATUS requests."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import time

import serial
from serial.tools import list_ports

from management_status_monitor import (
    GET_STATUS, GET_STATUS_RESPONSE, MAGIC, ManagementFrameParser,
    encode_management_frame, select_management_port,
)


SCENARIOS = (
    "normal_status", "bytewise_status", "noise_prefix_70_bytes",
    "bad_crc_then_valid", "oversized_header_then_valid",
    "maximum_payload_invalid_status", "normal_after_maximum",
)


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def collect(cdc, parser, duration, stop_on_frame):
    frames = []
    received_bytes = 0
    deadline = time.perf_counter() + duration
    while time.perf_counter() < deadline:
        data = cdc.read(max(1, min(cdc.in_waiting, 4096)))
        received_bytes += len(data)
        frames.extend(parser.feed(data))
        if frames and stop_on_frame:
            break
    return frames, received_bytes


def checked_write(cdc, data):
    written = cdc.write(data)
    if written != len(data):
        raise RuntimeError(f"Short USB write: {written}/{len(data)}")


def validate_response(frames, transaction_id, expected_result):
    if len(frames) != 1:
        raise AssertionError(f"Expected one response, received {len(frames)}")
    frame = frames[0]
    if frame.message_type != GET_STATUS_RESPONSE or frame.transaction_id != transaction_id:
        raise AssertionError("Response type or transaction ID mismatch")
    expected_length = 17 if expected_result == 0 else 2
    if len(frame.payload) != expected_length:
        raise AssertionError(f"Unexpected payload length {len(frame.payload)}")
    result = struct.unpack_from("<H", frame.payload)[0]
    if result != expected_result:
        raise AssertionError(f"Expected result {expected_result}, received {result}")
    status = {"result_code": result, "response_hex": frame.raw.hex(" ")}
    if result == 0:
        status.update(
            ethernet_link=frame.payload[2], ipv4=".".join(str(value) for value in frame.payload[3:7]),
            sntp_synchronized=frame.payload[7], mqtt_state=frame.payload[16],
        )
    return status


def exercise(cdc, parser, scenario, transaction_id, timeout, observation):
    valid = encode_management_frame(GET_STATUS, transaction_id)
    observation.update(transaction_id=transaction_id, sent_bytes=0)
    expected_result = 0
    if scenario == "bad_crc_then_valid":
        invalid = bytearray(encode_management_frame(GET_STATUS, transaction_id - 1))
        invalid[-1] ^= 0x01
        checked_write(cdc, invalid)
        frames, byte_count = collect(cdc, parser, 0.4, False)
        observation.update(sent_bytes=len(invalid), bad_crc_response_count=len(frames), bad_crc_rx_bytes=byte_count)
        observation["bad_crc_responses"] = [frame.raw.hex(" ") for frame in frames]
        if frames:
            raise AssertionError("Malformed CRC produced a protocol response")
    if scenario == "bytewise_status":
        for value in valid:
            checked_write(cdc, bytes((value,)))
            time.sleep(0.01)
        observation.update(sent_bytes=len(valid), write_calls=len(valid), inter_write_delay_ms=10)
    else:
        wire = valid
        if scenario == "noise_prefix_70_bytes":
            wire = b"\xa5" * 70 + valid
        elif scenario == "oversized_header_then_valid":
            wire = MAGIC + bytes((GET_STATUS,)) + struct.pack("<II", transaction_id - 1, 8193) + valid
        elif scenario == "maximum_payload_invalid_status":
            wire = encode_management_frame(GET_STATUS, transaction_id, bytes(8192))
            expected_result = 1
        checked_write(cdc, wire)
        observation["sent_bytes"] += len(wire)
    frames, byte_count = collect(cdc, parser, timeout, True)
    observation["received_bytes"] = byte_count
    observation["received_frames"] = [frame.raw.hex(" ") for frame in frames]
    observation.update(validate_response(frames, transaction_id, expected_result))
    extra_frames, extra_bytes = collect(cdc, parser, 0.08, False)
    observation.update(extra_response_count=len(extra_frames), extra_rx_bytes=extra_bytes)
    observation["extra_responses"] = [frame.raw.hex(" ") for frame in extra_frames]
    if extra_frames:
        raise AssertionError("Unexpected additional protocol response")
    return observation


def main():
    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("--port")
    argument_parser.add_argument("--rounds", type=int, default=3)
    argument_parser.add_argument("--timeout", type=float, default=2.0)
    argument_parser.add_argument("--output", type=Path, required=True)
    args = argument_parser.parse_args()
    if not 1 <= args.rounds <= 10 or not 0 < args.timeout <= 10:
        argument_parser.error("Use 1..10 rounds and a positive timeout up to 10 seconds")
    expected = bytes.fromhex("4d42475703785634120000000051e4c00e")
    if encode_management_frame(GET_STATUS, 0x12345678) != expected:
        raise AssertionError("Encoder disagrees with the specification GET_STATUS example")
    port = args.port or select_management_port(list_ports)
    identities = [{"device": item.device, "vid": item.vid, "pid": item.pid, "description": item.description}
                  for item in list_ports.comports() if item.device.upper() == port.upper()]
    if len(identities) != 1 or (identities[0]["vid"], identities[0]["pid"]) != (0x0483, 0x5740):
        raise RuntimeError("Selected port does not match the gateway Management USB VID/PID")
    args.output.mkdir(parents=True, exist_ok=False)
    metadata = {
        "started_utc": utc_now(), "port": port, "usb_identity": identities[0], "baud_rate": 115200,
        "baud_rate_meaning": "USB CDC host line coding; does not set the RTU baud rate",
        "rounds": args.rounds, "timeout_seconds": args.timeout, "scenarios": SCENARIOS,
        "commands": ["GET_STATUS (0x03)"], "firmware_changed": False, "device_reset_requested": False,
        "firmware_revision_verified": False,
        "maximum_payload_case": "Valid 8192-byte frame payload with GET_STATUS; expects INVALID_REQUEST because GET_STATUS requires empty payload",
        "fragmentation_scope": "Separate host writes; no logic analyzer or USB packet capture",
        "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False), encoding="utf-8")
    records = []
    session_error = None
    with (args.output / "cases.jsonl").open("x", encoding="utf-8") as log:
        try:
            with serial.Serial(port, baudrate=115200, timeout=0.02, write_timeout=args.timeout) as cdc:
                parser = ManagementFrameParser()
                stale, byte_count = collect(cdc, parser, 0.1, False)
                if stale:
                    raise RuntimeError("Received a protocol response before the first query; another session may be active")
                for round_number in range(1, args.rounds + 1):
                    for index, scenario in enumerate(SCENARIOS):
                        record = {"round": round_number, "scenario": scenario, "started_utc": utc_now()}
                        transaction_id = 0x60000000 + round_number * 100 + index * 2
                        started = time.perf_counter()
                        try:
                            exercise(cdc, parser, scenario, transaction_id, args.timeout, record)
                            record["outcome"] = "pass"
                        except Exception as error:
                            record.update(outcome="fail", detail=str(error))
                        record["elapsed_ms"] = (time.perf_counter() - started) * 1000
                        records.append(record)
                        log.write(json.dumps(record, ensure_ascii=False) + "\n")
                        log.flush()
                        print(f"Round {round_number}: {scenario}: {record['outcome']}", flush=True)
                        if record["outcome"] != "pass":
                            raise RuntimeError("Stopped after a failed case; subsequent cases were not executed")
        except Exception as error:
            session_error = str(error)
    successful = sum(record["outcome"] == "pass" for record in records)
    summary = {
        "ended_utc": utc_now(), "planned_cases": args.rounds * len(SCENARIOS),
        "executed_cases": len(records), "passed_cases": successful,
        "complete": len(records) == args.rounds * len(SCENARIOS), "session_error": session_error,
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(summary, indent=2), flush=True)
    return 0 if summary["complete"] and successful == len(records) and session_error is None else 1


if __name__ == "__main__":
    raise SystemExit(main())
