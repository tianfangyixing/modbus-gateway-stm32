#!/usr/bin/env python3
"""Measure read-only FC04 transactions over the gateway's TCP-to-RTU path.

Uses persistent connections, one outstanding request per connection, distinct
transaction-ID ranges per client, and a fixed start-to-start polling interval.
Every attempted request is retained in CSV, including failures. Latency excludes
TCP connection setup and includes network, gateway queues, and slave response.
It is not UART turnaround time, CPU utilization, or maximum server throughput.
"""

import argparse
import csv
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import hashlib
import ipaddress
import json
import math
from pathlib import Path
import platform
import queue
import socket
import statistics
import struct
import threading
import time


FIELDS = [
    "client", "sequence", "transaction_id", "started_utc", "outcome",
    "request_ms", "attempt_ms", "connect_ms", "connection_number",
    "start_lateness_ms", "missed_intervals_after", "values", "exception_code",
    "request_hex", "response_hex", "detail",
]


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


class ResponseError(Exception):
    def __init__(self, outcome, detail, response_hex="", exception_code=""):
        super().__init__(detail)
        self.outcome = outcome
        self.response_hex = response_hex
        self.exception_code = exception_code


def read_response(connection, transaction_id, unit_id, quantity, deadline):
    received = bytearray()

    def receive_exact(size):
        part = bytearray()
        while len(part) < size:
            remaining = deadline - time.perf_counter()
            if remaining <= 0:
                raise TimeoutError("Response deadline expired")
            connection.settimeout(remaining)
            chunk = connection.recv(size - len(part))
            if not chunk:
                raise ConnectionError("Peer closed the connection")
            part.extend(chunk)
            received.extend(chunk)
        return bytes(part)

    try:
        header = receive_exact(7)
        response_id, protocol_id, length, response_unit = struct.unpack(">HHHB", header)
        if response_id != transaction_id or protocol_id != 0 or response_unit != unit_id:
            raise ResponseError("protocol_error", "MBAP transaction/protocol/unit mismatch", received.hex(" "))
        if not 2 <= length <= 254:
            raise ResponseError("protocol_error", "Invalid MBAP length", received.hex(" "))
        pdu = receive_exact(length - 1)
        if pdu[0] == 0x84 and len(pdu) == 2:
            code = f"0x{pdu[1]:02x}"
            raise ResponseError("modbus_exception", f"FC04 exception {code}", received.hex(" "), code)
        if len(pdu) != 2 + 2 * quantity or pdu[0] != 0x04 or pdu[1] != 2 * quantity:
            raise ResponseError("protocol_error", "FC04 function/byte count/length mismatch", received.hex(" "))
        values = struct.unpack(f">{quantity}H", pdu[2:])
        return values, received.hex(" ")
    except TimeoutError as error:
        raise ResponseError("timeout", str(error), received.hex(" ")) from error
    except OSError as error:
        raise ResponseError("connection_error", str(error), received.hex(" ")) from error


def percentile_nearest_rank(values, percentile):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * percentile / 100) - 1)]


def summarize(records):
    counts = Counter(row["outcome"] for row in records)
    latencies = [row["request_ms"] for row in records if row["outcome"] == "ok"]
    return {
        "attempted": len(records),
        "successful": counts["ok"],
        "success_rate_percent": 100 * counts["ok"] / len(records) if records else None,
        "outcomes": dict(sorted(counts.items())),
        "exception_codes": dict(Counter(row["exception_code"] for row in records if row["exception_code"])),
        "successful_request_latency_ms": {
            "min": min(latencies) if latencies else None,
            "mean": statistics.mean(latencies) if latencies else None,
            "p50": percentile_nearest_rank(latencies, 50),
            "p95": percentile_nearest_rank(latencies, 95),
            "p99": percentile_nearest_rank(latencies, 99),
            "max": max(latencies) if latencies else None,
        },
        "connection_attempts": sum(row["connect_ms"] != "" for row in records),
        "missed_poll_intervals": sum(row["missed_intervals_after"] for row in records),
    }


def run_client(client, args, scheduled_start, output, stop):
    connection = None
    connection_number = 0
    consecutive_errors = 0
    next_start = scheduled_start
    try:
        for sequence in range(1, args.requests_per_client + 1):
            if stop.wait(max(0, next_start - time.perf_counter())):
                break
            transaction_id = (client - 1) * 16384 + ((sequence - 1) % 16383) + 1
            request = struct.pack(
                ">HHHBBHH", transaction_id, 0, 6, args.unit, 0x04, args.start, args.quantity
            )
            attempt_start = time.perf_counter()
            row = {field: "" for field in FIELDS}
            row.update(
                client=client, sequence=sequence, transaction_id=transaction_id,
                started_utc=utc_now(), start_lateness_ms=max(0, attempt_start - next_start) * 1000,
                request_hex=request.hex(" "), outcome="internal_error",
            )
            request_start = None
            try:
                if connection is None:
                    connection_number += 1
                    connect_start = time.perf_counter()
                    try:
                        connection = socket.create_connection((args.host, args.port), args.timeout)
                        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    finally:
                        row["connect_ms"] = (time.perf_counter() - connect_start) * 1000
                row["connection_number"] = connection_number
                connection.settimeout(args.timeout)
                request_start = time.perf_counter()
                connection.sendall(request)
                values, response_hex = read_response(
                    connection, transaction_id, args.unit, args.quantity, request_start + args.timeout
                )
                row.update(outcome="ok", values=";".join(str(value) for value in values), response_hex=response_hex)
            except ResponseError as error:
                row.update(
                    outcome=error.outcome, detail=str(error), response_hex=error.response_hex,
                    exception_code=error.exception_code,
                )
            except TimeoutError as error:
                row.update(outcome="connect_timeout" if request_start is None else "timeout", detail=str(error))
            except OSError as error:
                row.update(outcome="connect_error" if request_start is None else "connection_error", detail=str(error))
            finally:
                finished = time.perf_counter()
                row["connection_number"] = connection_number
                row["attempt_ms"] = (finished - attempt_start) * 1000
                if request_start is not None:
                    row["request_ms"] = (finished - request_start) * 1000
                next_start += args.interval
                missed = max(0, math.floor((finished - next_start) / args.interval) + 1)
                next_start += missed * args.interval
                row["missed_intervals_after"] = missed
                output.put(row)
            if row["outcome"] == "ok":
                consecutive_errors = 0
            else:
                consecutive_errors += 1
                if row["outcome"] != "modbus_exception" and connection is not None:
                    connection.close()
                    connection = None
                if consecutive_errors >= args.stop_after_errors:
                    stop.set()
    finally:
        if connection is not None:
            connection.close()
        output.put(None)


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=502)
    parser.add_argument("--clients", type=int, choices=range(1, 5), default=1)
    parser.add_argument("--requests-per-client", type=int, default=1000)
    parser.add_argument("--interval", type=float, default=5.0, help="Seconds between request starts per client")
    parser.add_argument("--timeout", type=float, default=3.0, help="Separate connect and request deadlines in seconds")
    parser.add_argument("--unit", type=lambda value: int(value, 0), default=1)
    parser.add_argument("--start", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--quantity", type=int, default=2)
    parser.add_argument("--stop-after-errors", type=int, default=5, help="Stop the run after this many consecutive errors in any client")
    parser.add_argument("--output", type=Path, required=True, help="New directory for metadata, CSV, and summary")
    parser.add_argument("--notes", default="")
    args = parser.parse_args()
    try:
        ipaddress.IPv4Address(args.host)
    except ipaddress.AddressValueError:
        parser.error("--host must be an IPv4 address")
    if not 1 <= args.port <= 65535 or not 1 <= args.unit <= 247:
        parser.error("Invalid port or unit ID")
    if not 1 <= args.quantity <= 125 or not 0 <= args.start <= 65536 - args.quantity:
        parser.error("Invalid register range")
    if args.requests_per_client < 1 or args.stop_after_errors < 1:
        parser.error("Request and error counts must be positive")
    if not math.isfinite(args.interval) or not math.isfinite(args.timeout) or args.interval <= 0 or args.timeout <= 0:
        parser.error("Interval and timeout must be finite positive seconds")
    return args


def main():
    args = parse_arguments()
    args.output.mkdir(parents=True, exist_ok=False)
    records = []
    stop = threading.Event()
    output = queue.Queue()
    started_utc = utc_now()
    scheduled_start = time.perf_counter() + 0.2
    metadata = {
        "started_utc": started_utc,
        "settings": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
        "function": "0x04 Read Input Registers",
        "serial_settings_user_reported": {"baud": 9600, "format": "8N1"},
        "serial_settings_changed_by_tool": False,
        "firmware_revision_verified": False,
        "platform": platform.platform(),
        "python": platform.python_version(),
        "tool_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "latency_definition": "Before TCP sendall to complete validated response; excludes connect; includes gateway/RTU/slave waits",
        "percentile_definition": "Nearest rank over successful requests only; errors retained separately",
        "scheduling": "Fixed per-client start interval; missed slots are counted and skipped; no catch-up bursts",
        "retry_policy": "No retry within a request; next scheduled request reconnects after transport/protocol failure",
        "data_validation": "Checks transaction ID, protocol ID, unit ID, FC04, length and byte count; physical sensor accuracy is not verified",
    }
    (args.output / "metadata.json").write_text(json.dumps(metadata, indent=2, ensure_ascii=False), encoding="utf-8")
    interrupted = False
    harness_errors = []
    print(f"Starting {args.clients} clients, {args.requests_per_client} requests each, interval={args.interval}s", flush=True)
    with (args.output / "requests.csv").open("x", newline="", encoding="utf-8-sig") as log:
        writer = csv.DictWriter(log, fieldnames=FIELDS)
        writer.writeheader()
        log.flush()
        with ThreadPoolExecutor(max_workers=args.clients) as executor:
            futures = [executor.submit(run_client, client, args, scheduled_start, output, stop)
                       for client in range(1, args.clients + 1)]
            finished_clients = 0
            last_progress = time.perf_counter()
            while finished_clients < args.clients:
                try:
                    row = output.get(timeout=0.5)
                    if row is None:
                        finished_clients += 1
                    else:
                        writer.writerow(row)
                        log.flush()
                        records.append(row)
                except queue.Empty:
                    row = None
                except KeyboardInterrupt:
                    interrupted = True
                    stop.set()
                    print("Stopping; retaining every completed attempt.", flush=True)
                now = time.perf_counter()
                if now - last_progress >= 30:
                    counts = Counter(record["outcome"] for record in records)
                    print(f"Progress: {len(records)} attempts, outcomes={dict(counts)}", flush=True)
                    last_progress = now
            for future in futures:
                try:
                    future.result()
                except Exception as error:
                    harness_errors.append(repr(error))
    elapsed = max(0, time.perf_counter() - scheduled_start)
    total = summarize(records)
    per_client = {str(client): summarize([row for row in records if row["client"] == client])
                  for client in range(1, args.clients + 1)}
    complete = len(records) == args.clients * args.requests_per_client and not interrupted and not harness_errors
    summary = {
        "started_utc": started_utc, "ended_utc": utc_now(), "elapsed_seconds": elapsed,
        "planned_requests": args.clients * args.requests_per_client, "complete": complete,
        "interrupted": interrupted, "harness_errors": harness_errors,
        "stopped_after_consecutive_errors": stop.is_set() and not interrupted,
        "aggregate": total, "clients": per_client,
        "observed_successful_requests_per_second": total["successful"] / elapsed if elapsed > 0 else None,
        "capacity_claim": "This is observed rate at the configured polling interval, not maximum throughput",
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(json.dumps(summary, indent=2), flush=True)
    print(f"Artifacts: {args.output.resolve()}", flush=True)
    return 0 if complete and total["successful"] == total["attempted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
