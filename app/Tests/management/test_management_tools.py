"""Independent wire vectors and host-tool framing regression; no serial device required."""
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "Management" / "tools"))
import management_status_monitor as wire

STATUS = bytes.fromhex("4d42475703785634120000000051e4c00e")


class ManagementToolsTests(unittest.TestCase):
    def test_fixed_vector(self):
        self.assertEqual(wire.encode_management_frame(3, 0x12345678), STATUS)
        self.assertEqual(zlib.crc32(STATUS[:13]), 0x0EC0E451)
        self.assertEqual(zlib.crc32(b"123456789"), 0xCBF43926)

    def test_maximum_and_overlap_independent_wire(self):
        self.assertEqual(wire.MAX_PAYLOAD_LENGTH, 8477)
        self.assertEqual(wire.MAX_FRAME_LENGTH, 8494)
        body = b"MBGW" + bytes([3]) + struct.pack("<II", 123, 8477) + b"\xa5" * 8477
        expected = body + struct.pack("<I", zlib.crc32(body))
        self.assertEqual(wire.encode_management_frame(3, 123, b"\xa5" * 8477), expected)
        for chunk in (1, 3, 7, 13, 64, 127, 2048, 8494):
            parser = wire.ManagementFrameParser()
            frames = []
            for start in range(0, len(expected), chunk):
                frames.extend(parser.feed(expected[start:start + chunk]))
            self.assertEqual(len(frames), 1)
            self.assertEqual(frames[0].raw, expected)
        with self.assertRaises(ValueError):
            wire.encode_management_frame(3, 0, bytes(8478))

    def test_noise_crc_oversize_glued_and_session(self):
        for declared in (8478, 0xFFFFFFFF):
            parser = wire.ManagementFrameParser()
            invalid = b"MBGW" + bytes([3]) + struct.pack("<II", 5, declared)
            self.assertEqual(parser.feed(invalid), [])
            self.assertEqual(parser.feed(STATUS)[0].raw, STATUS)
        parser = wire.ManagementFrameParser()
        self.assertEqual(parser.feed(STATUS[:-1] + bytes([STATUS[-1] ^ 1])), [])
        self.assertEqual(parser.feed(b"noiseMB" + STATUS + STATUS)[0].raw, STATUS)
        parser.reset()
        self.assertEqual(parser.feed(STATUS[:12]), [])
        parser.reset()
        self.assertEqual(parser.feed(STATUS[12:]), [])
        self.assertEqual(parser.feed(STATUS)[0].raw, STATUS)

    def test_readonly_scenarios_only_send_status(self):
        import management_readonly_validation as validator
        class FakeCdc:
            def __init__(self):
                self.writes = []
                self.requests = wire.ManagementFrameParser()
                self.pending = bytearray()
            def write(self, data):
                self.writes.append(bytes(data))
                for request in self.requests.feed(data):
                    self.assert_status(request)
                    payload = b"\x01\x00" if request.payload else bytes(17)
                    self.pending.extend(wire.encode_management_frame(0x83, request.transaction_id, payload))
                return len(data)
            @staticmethod
            def assert_status(request):
                if request.message_type != 3:
                    raise AssertionError("Read-only validator sent a mutating/non-status command")
            @property
            def in_waiting(self):
                return len(self.pending)
            def read(self, count):
                data = bytes(self.pending[:count])
                del self.pending[:count]
                return data
        for scenario in validator.SCENARIOS:
            cdc = FakeCdc()
            result = validator.exercise(cdc, wire.ManagementFrameParser(), scenario, 123, 0.05, {})
            self.assertEqual(result["extra_response_count"], 0)
            if scenario == "maximum_payload_invalid_status":
                self.assertEqual(len(cdc.writes[0]), 8494)
                self.assertEqual(result["result_code"], 1)
            if scenario == "oversized_header_then_valid":
                self.assertEqual(struct.unpack_from("<I", cdc.writes[0], 9)[0], 8478)


if __name__ == "__main__":
    unittest.main()
