"""Verify bytes from the real C publisher/MQTT path; optionally replay to a local broker."""
import argparse
import base64
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import tempfile


def verify(packet, derived):
    expected_id = (b"STM" + base64.b32encode(bytes.fromhex("0123456789abcdef10203040")).rstrip(b"=")) if derived else (b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-" * 4)
    assert len(packet) == (814 if derived else 1047)
    assert packet[0] == 0x10
    assert packet[1:3] == (bytes.fromhex("ab06") if derived else bytes.fromhex("9408"))
    assert packet[3:13] == bytes.fromhex("00044d51545404f6003c")
    expected = [expected_id, b"T" * 128, b"P" * 128,
                bytes(32 + i % 95 for i in range(256)), bytes(126 - i % 95 for i in range(256))]
    offset = 13
    for value in expected:
        length, = struct.unpack_from(">H", packet, offset)
        offset += 2
        assert length == len(value)
        assert packet[offset:offset + length] == value
        offset += length
    assert offset == len(packet)
    return packet


def replay(packet, port, ca, server_name):
    # Explicit opt-in; only contacts loopback. The broker must permit the synthetic credentials.
    with socket.create_connection(("127.0.0.1", port), timeout=5) as raw:
        if ca:
            context = ssl.create_default_context(cafile=ca)
            connection = context.wrap_socket(raw, server_hostname=server_name)
        else:
            connection = raw
        with connection:
            connection.sendall(packet)
            reply = bytearray()
            while len(reply) < 4:
                chunk = connection.recv(4 - len(reply))
                if not chunk:
                    raise RuntimeError("Broker closed before CONNACK")
                reply.extend(chunk)
            if reply != bytes.fromhex("20020000"):
                raise RuntimeError(f"Broker rejected CONNECT: CONNACK={reply.hex()}")
            connection.sendall(bytes.fromhex("e000"))
    print("PASS local broker accepted maximum credentials and will parameters")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--broker-port", type=int, help="Opt in to loopback broker replay")
    parser.add_argument("--ca", help="Local TLS broker CA PEM; enables verified TLS")
    parser.add_argument("--server-name", default="localhost")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mqtt-v2-") as temporary:
        for derived in (False, True):
            mode = "derived" if derived else "explicit"
            output = Path(temporary) / f"{mode}.bin"
            subprocess.run([str(args.executable.resolve()), mode, str(output)], check=True)
            packet = verify(output.read_bytes(), derived)
            print(f"PASS independent {mode} wire oracle: {len(packet)} bytes, all five strings intact")
            if args.broker_port and not derived:
                replay(packet, args.broker_port, args.ca, args.server_name)
    if not args.broker_port:
        print("External broker replay not requested; transport-boundary tests only")


if __name__ == "__main__":
    main()
