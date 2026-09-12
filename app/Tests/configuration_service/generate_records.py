"""Independent persisted-record vectors: wire fixtures + Python standard CRC32.

No production C codec/CRC/generation helper is used to create expectations.
"""
import argparse
import struct
import zlib
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--fixtures", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
default = (args.fixtures / "default_v2.bin").read_bytes()
maximum = (args.fixtures / "maximum_v2.bin").read_bytes()
assert len(default) == 48 and default[0] == 2
assert len(maximum) == 8475 and maximum[0] == 2
alternate = bytearray(default)
assert alternate[9:11] == b"\xf6\x01"
alternate[9:11] = b"\xf7\x01"  # Modbus TCP port 503, independent wire edit.
legacy = bytes([1]) + default[1:]

def record(payload, generation, magic=0x32474643, length=None):
    prefix = struct.pack("<IIII", magic, generation,
                         len(payload) if length is None else length, zlib.crc32(payload))
    return prefix + struct.pack("<I", zlib.crc32(prefix)) + payload

vectors = {"default_payload": default, "alternate_payload": alternate,
           "maximum_payload": maximum, "legacy_payload": legacy}
for name, payload in (("default", default), ("alternate", alternate), ("maximum", maximum)):
    for generation in range(3):
        vectors[f"{name}_record_{generation}"] = record(payload, generation)
vectors["legacy_record"] = record(legacy, 0, magic=0x57753102)
vectors["legacy_in_cfg2_record"] = record(legacy, 0)
for length in (0, 8476, 12268, 0xffffffff):
    vectors[f"length_{length}_header"] = record(default, 0, length=length)[:20]
vectors["invalid_generation_header"] = record(default, 3)[:20]
lines = ["/* Generated from fixed payloads and Python zlib; do not hand edit. */",
         "#ifndef STORAGE_RECORD_FIXTURES_H", "#define STORAGE_RECORD_FIXTURES_H", "#include <stdint.h>"]
for name, value in vectors.items():
    lines += [f"static const uint8_t {name}[{len(value)}] =", "{"]
    for start in range(0, len(value), 16):
        lines.append("    " + ", ".join(f"0x{x:02X}" for x in value[start:start + 16]) + ",")
    lines += ["};", ""]
lines += ["#endif", ""]
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n".join(lines), encoding="utf-8")
