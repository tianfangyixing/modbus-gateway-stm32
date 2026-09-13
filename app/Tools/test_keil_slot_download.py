"""Real download policy with a fake SWD/Flash boundary; never connects to a board."""
import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import keil_slot_download as tool


def record(generation=0, attempted=255, success=255):
    return bytes.fromhex('53 4c 54 32') + bytes([generation, 1, attempted, success])


def firmware(slot, length=1024):
    return struct.pack('<II', 0x20010000, tool.SLOTS[slot] + 521) + b'\x42' * (length - 8)


class FakeProbe:
    def __init__(self, slot='A', fault=None):
        self.memory = bytearray(b'\xff' * tool.FLASH_SIZE)
        for offset in [0, 0x10000, 0xe0000]:
            self.memory[offset:offset + 32] = b'R' * 32
        other = 'B' if slot == 'A' else 'A'
        offset = tool.SLOTS[other] - tool.FLASH_BASE
        self.memory[offset:offset + 8] = record(2, 1, 1)
        self.slot, self.fault = slot, fault
        self.events, self.is_halted, self.vtor = [], False, 0

    def read(self, address, size):
        if address == 0x1FFF7A22:
            return b'\x00\x04'
        offset = address - tool.FLASH_BASE
        self.events.append(('read', address, size))
        return bytes(self.memory[offset:offset + size])

    def read32(self, address):
        return {0xE0042000: 0x10076413, 0x40023C14: 0x0FFFAAED, 0xE000ED08: self.vtor}[address]

    def halted(self):
        return self.is_halted

    def halt(self):
        self.is_halted = True

    def go(self):
        self.is_halted = False

    def reset_halt(self):
        self.halt()

    def program_image(self, address, data):
        self.events.append(('image', address, len(data)))
        if self.fault == 'download':
            raise RuntimeError('Injected loader failure')
        offset = address - tool.FLASH_BASE
        self.memory[offset:offset + len(data)] = data
        if self.fault == 'body':
            self.memory[offset + 80] ^= 1
        if self.fault == 'outside':
            self.memory[0x10000] ^= 1

    def clear_boot_request(self):
        self.events.append(('clear_request',))

    def reset_run(self):
        self.events.append(('reset_run',))
        offset = tool.SLOTS[self.slot] - tool.FLASH_BASE
        self.memory[offset + 6:offset + 8] = b'\x01\x01'
        self.vtor = tool.SLOTS[self.slot] + 512
        self.go()


class FakeFlash:
    def __init__(self, probe):
        self.probe = probe

    def erase(self, sector):
        self.probe.events.append(('erase', sector))
        offset = 0x20000 + (sector - 5) * 0x20000
        self.probe.memory[offset:offset + 0x20000] = b'\xff' * 0x20000

    def flush(self):
        pass

    def program_header(self, address, data):
        self.probe.events.append(('header', address, data))
        offset = address - tool.FLASH_BASE
        if self.probe.fault == 'header' and address == tool.SLOTS[self.probe.slot]:
            raise RuntimeError('Injected initialization failure')
        self.probe.memory[offset:offset + len(data)] = data


class DownloadTests(unittest.TestCase):
    def run_download(self, probe, slot, data=None):
        with tempfile.TemporaryDirectory() as temp, patch.object(tool, 'Flash', FakeFlash):
            with contextlib.redirect_stdout(io.StringIO()):
                return tool.download(probe, slot, data or firmware(slot), Path(temp))

    def test_header_states(self):
        cases = [(b'\xff' * 8, 'invalid'), (record(), 'pending'),
                 (record(1, 1), 'unconfirmed'), (record(2, 1, 1), 'confirmed'),
                 (record(3), 'invalid'), (record(0, 255, 1), 'invalid'),
                 (record()[:5] + b'\xff\xff\xff', 'invalid')]
        for header, state in cases:
            with self.subTest(header=header):
                self.assertEqual(tool.slot_state(header), state)

    def test_new_download_beats_every_other_generation(self):
        for slot in 'AB':
            other = 'B' if slot == 'A' else 'A'
            for previous, expected in [(0, 1), (1, 2), (2, 0)]:
                for own in range(3):
                    headers = {slot: record(own, 1, 1), other: record(previous, 1, 1)}
                    self.assertEqual(tool.plan_download(slot, firmware(slot), headers)['generation'], expected)

    def test_erasure_boundaries(self):
        headers = {'A': b'\xff' * 8, 'B': b'\xff' * 8}
        for slot, first in [('A', 5), ('B', 8)]:
            for size, count in [(1024, 1), (0x1FE00, 1), (0x1FE01, 2), (0x5FE00, 3)]:
                plan = tool.plan_download(slot, firmware(slot, size), headers)
                self.assertEqual(plan['sectors'], list(range(first, first + count)))
                self.assertEqual(bytes.fromhex(plan['header']), record())

    def test_wrong_slot_vectors_rejected_before_writes(self):
        probe = FakeProbe()
        with self.assertRaises(ValueError):
            self.run_download(probe, 'A', firmware('B'))
        self.assertFalse(probe.events)

    def test_commit_follows_full_readback_and_preserves_other_regions(self):
        for slot in 'AB':
            probe = FakeProbe(slot)
            before = bytes(probe.memory)
            result = self.run_download(probe, slot)
            self.assertTrue(result['confirmed'])
            self.assertFalse(probe.halted())
            offset = tool.SLOTS[slot] - tool.FLASH_BASE
            self.assertEqual(probe.memory[:offset], before[:offset])
            self.assertEqual(probe.memory[offset + 0x20000:], before[offset + 0x20000:])
            writes = [e for e in probe.events if e[0] == 'header']
            self.assertEqual(writes, [('header', tool.SLOTS[slot], record()[:5]),
                                     ('header', tool.SLOTS[slot] + 5, b'\x01')])
            first_commit = probe.events.index(writes[0])
            image = next(i for i, e in enumerate(probe.events) if e[0] == 'image')
            self.assertIn(('read', tool.FLASH_BASE, tool.FLASH_SIZE), probe.events[image:first_commit])

    def test_loader_and_verification_failures_never_commit(self):
        for fault in ['download', 'body', 'outside']:
            probe = FakeProbe(fault=fault)
            with self.subTest(fault=fault), self.assertRaises(RuntimeError):
                self.run_download(probe, 'A')
            self.assertFalse(any(e[0] in ('header', 'reset_run') for e in probe.events))
            self.assertEqual(probe.memory[0x20000:0x20008], b'\xff' * 8)

    def test_header_initialization_failure_never_commits_done(self):
        probe = FakeProbe(fault='header')
        with self.assertRaises(RuntimeError):
            self.run_download(probe, 'A')
        self.assertEqual(probe.memory[0x20005], 255)
        self.assertFalse(any(e[0] == 'reset_run' for e in probe.events))

    def test_raw_flash_access_cannot_erase_bootloader_or_reserved_sectors(self):
        flash = tool.Flash(None)
        for sector in [0, 3, 4, 11, 12]:
            with self.assertRaises(ValueError):
                flash.erase(sector)
        with self.assertRaises(ValueError):
            flash.program_header(0x08020200, b'\x00')

    def test_keil_download_configuration(self):
        project = ET.parse(tool.ROOT / 'app/MDK-ARM/modbus-gateway-stm32.uvprojx')
        for target in project.findall('.//Targets/Target'):
            slot = target.findtext('TargetName')[-1]
            utilities = target.find('TargetOption/Utilities')
            self.assertEqual(utilities.findtext('Flash1/UseExternalTool'), '1')
            self.assertEqual(utilities.findtext('Flash1/UpdateFlashBeforeDebugging'), '0')
            self.assertIn('keil_slot_download.py', utilities.findtext('Flash3'))
            self.assertIn('--slot ' + slot, utilities.findtext('Flash3'))



class RegisterProbe:
    def __init__(self, fail=False):
        self.values = {tool.FLASH_CR: tool.LOCK, tool.FLASH_SR: 0, tool.FLASH_ACR: 0x600}
        self.memory = bytearray(b"\xff" * 512 + b"APP_CANARY")
        self.events, self.fail = [], fail

    def halted(self):
        return True

    def command(self, text):
        self.events.append(("command", text))

    def read32(self, address):
        return self.values[address]

    def write32(self, address, value):
        self.events.append(("register", address, value))
        if address == tool.FLASH_KEYR and value == 0xCDEF89AB:
            self.values[tool.FLASH_CR] = 0
        elif address == tool.FLASH_SR:
            self.values[address] &= ~value
        else:
            self.values[address] = value

    def read(self, address, length):
        offset = address - tool.SLOTS["A"]
        return bytes(self.memory[offset:offset + length])

    def write(self, address, data):
        if self.values[tool.FLASH_CR] != 1:
            raise RuntimeError("Header must use only PG=x8")
        offset = address - tool.SLOTS["A"]
        self.memory[offset:offset + len(data)] = data
        if self.fail:
            self.values[tool.FLASH_SR] = 0x80


class RegisterTests(unittest.TestCase):
    def test_header_commit_never_enables_erase_and_preserves_app(self):
        probe = RegisterProbe()
        flash = tool.Flash(probe)
        flash.program_header(tool.SLOTS["A"], record(2)[:5])
        flash.program_header(tool.SLOTS["A"] + 5, b"\x01")
        self.assertEqual(probe.memory[:8], record(2))
        self.assertEqual(probe.memory[512:], b"APP_CANARY")
        self.assertEqual(probe.values[tool.FLASH_CR], tool.LOCK)
        self.assertTrue(all(event[2] in (1, tool.LOCK) for event in probe.events
                            if event[:2] == ("register", tool.FLASH_CR)))
        self.assertFalse(any(event == ("command", "EnableFlashDL") for event in probe.events))

    def test_program_error_still_locks_flash(self):
        probe = RegisterProbe(fail=True)
        with self.assertRaises(RuntimeError):
            tool.Flash(probe).program_header(tool.SLOTS["A"], record()[:5])
        self.assertEqual(probe.values[tool.FLASH_CR], tool.LOCK)
        self.assertEqual(probe.memory[5], 255)

    def test_non_erased_header_refused_before_unlock(self):
        probe = RegisterProbe()
        probe.memory[0] = 0
        with self.assertRaises(RuntimeError):
            tool.Flash(probe).program_header(tool.SLOTS["A"], record()[:5])
        self.assertFalse(probe.events)


if __name__ == '__main__':
    unittest.main()
