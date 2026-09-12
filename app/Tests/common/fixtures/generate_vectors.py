"""Reproduce fixed v2 vectors from the published field table, without the C codec.
The checked-in public root certificate is input. No keys/network are needed.
"""
from pathlib import Path
from struct import pack

HERE = Path(__file__).resolve().parent

def text(value):
    return pack("<H", len(value)) + value

def hostname(initial):
    return initial + b"a" * 62 + b"." + b"a" * 63 + b"." + b"a" * 63 + b"." + b"a" * 61

def message(topic_initial, payload_value):
    return b"\x01" + text(topic_initial + b"t" * 127) + text(payload_value * 128) + b"\x02\x01"

def main():
    default = bytes.fromhex(
        "02 00 80 25 00 00 03 E8 03 F6 01 00 0E 00 6E 74 "
        "70 2E 61 6C 69 79 75 6E 2E 63 6F 6D 00 0F 00 6E "
        "74 70 2E 74 65 6E 63 65 6E 74 2E 63 6F 6D 00 00")
    ca = (HERE / "root_ca_4096.pem").read_bytes()
    assert len(ca) == 4096 and b"\0" not in ca
    assert ca.count(b"-----BEGIN CERTIFICATE-----") == 1
    network = bytes.fromhex("01 C0 A8 01 0A FF FF FF 00 C0 A8 01 01 01 01 01 01 08 08 08 08")
    rtu = pack("<IBH", 115200, 0, 1000)
    sntp = b"\x00" + text(hostname(b"s")) + b"\x00" + text(hostname(b"t"))
    mqtt = (b"\x01" + text(hostname(b"m")) + pack("<H", 8883)
            + b"\x01" + text(b"C" * 254 + b"_-")
            + text(b" " + b"U" * 255) + text(b"P" * 255 + b"~")
            + text(ca) + pack("<H", 60) + message(b"O", b"o") + message(b"W", b"w"))
    collection = b"\x10" + b"".join(
        pack("<BBHBIH", i + 1, 2, i, 0, 1000, 50)
        + text(bytes([ord("a") + i]) + b"t" * 127) + b"\x01" for i in range(16))
    maximum = b"\x02" + network + rtu + pack("<H", 502) + sntp + mqtt + collection
    assert [len(network), len(rtu), len(sntp), len(mqtt), len(collection)] == [21, 7, 512, 5659, 2273]
    assert len(default) == 48 and len(maximum) == 8475
    (HERE / "default_v2.bin").write_bytes(default)
    (HERE / "maximum_v2.bin").write_bytes(maximum)
    print("Wrote independent default (48) and maximum (8475) vectors")

if __name__ == "__main__":
    main()
