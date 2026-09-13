#!/usr/bin/env python3
"""Keil/J-Link download: verify the application before committing its slot header."""

import argparse
import ctypes as ct
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import sys
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from hex_to_fwu import hex_to_bin, validate_vectors

FLASH_BASE = 0x08000000
FLASH_SIZE = 0x100000
SLOTS = {"A": 0x08020000, "B": 0x08080000}
SLOT_SIZE = 0x60000
HEADER_SIZE = 512
SECTOR_SIZE = 0x20000
MAGIC = 0x32544C53
FLASH_ACR, FLASH_KEYR, FLASH_SR, FLASH_CR = 0x40023C00, 0x40023C04, 0x40023C0C, 0x40023C10
BUSY, ERRORS, LOCK = 0x10000, 0xF2, 0x80000000


def slot_state(header):
    if len(header) != 8:
        raise ValueError("A complete 8-byte slot record is required")
    magic, generation, done, attempted, success = struct.unpack("<IBBBB", header)
    if magic != MAGIC or generation > 2 or done != 1:
        return "invalid"
    return {(255, 255): "pending", (1, 255): "unconfirmed", (1, 1): "confirmed"}.get(
        (attempted, success), "invalid")


def next_generation(headers, slot):
    other = "B" if slot == "A" else "A"
    if slot_state(headers[other]) in ("pending", "confirmed"):
        return (headers[other][4] + 1) % 3
    if slot_state(headers[slot]) in ("pending", "confirmed"):
        return (headers[slot][4] + 1) % 3
    return 0


def plan_download(slot, firmware, headers):
    if slot not in SLOTS:
        raise ValueError("Unknown slot")
    validate_vectors(firmware, SLOTS[slot] + HEADER_SIZE)
    for header in headers.values():
        slot_state(header)
    generation = next_generation(headers, slot)
    count = (HEADER_SIZE + len(firmware) + SECTOR_SIZE - 1) // SECTOR_SIZE
    first = 5 if slot == "A" else 8
    return {"slot": slot, "generation": generation, "sectors": list(range(first, first + count)),
            "header": struct.pack("<IBBBB", MAGIC, generation, 1, 255, 255).hex(),
            "app_address": SLOTS[slot] + HEADER_SIZE, "length": len(firmware),
            "sha256": hashlib.sha256(firmware).hexdigest()}


class JLink:
    """Small ctypes binding to the installed SEGGER DLL; no Python packages required."""

    def __init__(self, dll_path, serial, folder):
        self.dll = ct.CDLL(str(dll_path))
        self.opened = False
        self.log_file = (folder / "jlink.log").open("w", encoding="utf-8")
        callback = ct.CFUNCTYPE(None, ct.c_char_p)
        self.callback = callback(lambda s: self.log_file.write(s.decode(errors="replace") + "\n") if s else None)
        self.unsecure = ct.CFUNCTYPE(ct.c_int, ct.c_char_p, ct.c_char_p, ct.c_uint32)(lambda a, b, c: 0)
        signatures = {
            "OpenEx": (ct.c_char_p, [callback, callback]), "Close": (None, []),
            "EMU_SelectByUSBSN": (ct.c_int, [ct.c_uint32]),
            "ExecCommand": (ct.c_int, [ct.c_char_p, ct.c_void_p, ct.c_int]),
            "TIF_Select": (ct.c_int, [ct.c_int]), "SetSpeed": (None, [ct.c_int]),
            "Connect": (ct.c_int, []), "IsHalted": (ct.c_int, []), "Halt": (ct.c_int, []),
            "Go": (None, []), "Reset": (ct.c_int, []),
            "ReadMemEx": (ct.c_int, [ct.c_uint32, ct.c_uint32, ct.c_void_p, ct.c_uint32]),
            "WriteMemEx": (ct.c_int, [ct.c_uint32, ct.c_uint32, ct.c_void_p, ct.c_uint32]),
            "BeginDownload": (None, [ct.c_uint32]),
            "WriteMem": (ct.c_int, [ct.c_uint32, ct.c_uint32, ct.c_void_p]),
            "EndDownload": (ct.c_int, []),
        }
        for name, (restype, argtypes) in signatures.items():
            function = getattr(self.dll, "JLINKARM_" + name)
            function.restype, function.argtypes = restype, argtypes
        try:
            self.check(self.dll.JLINKARM_EMU_SelectByUSBSN(serial), "select J-Link")
            error = self.dll.JLINKARM_OpenEx(self.callback, self.callback)
            if error:
                raise RuntimeError(error.decode(errors="replace"))
            self.opened = True
            self.dll.JLINK_SetHookUnsecureDialog.argtypes = [type(self.unsecure)]
            self.dll.JLINK_SetHookUnsecureDialog(self.unsecure)
            self.command("InhibitConnectRetries = 1")
            self.command("DisableFlashBPs")
            self.command("ExcludeFlashCacheRange 0x08000000-0x080FFFFF")
            self.check(self.dll.JLINKARM_TIF_Select(1), "select SWD")
            self.dll.JLINKARM_SetSpeed(4000)
            self.command("Device = STM32F407ZGTx")
            self.check(self.dll.JLINKARM_Connect(), "connect STM32")
        except BaseException:
            self.close()
            raise

    @staticmethod
    def check(result, action):
        if result < 0:
            raise RuntimeError(f"J-Link {action} failed ({result})")
        return result

    def command(self, command):
        error = ct.create_string_buffer(1024)
        result = self.dll.JLINKARM_ExecCommand(command.encode(), error, len(error))
        if result != 0:
            raise RuntimeError(f"J-Link {command}: {error.value.decode(errors='replace')} ({result})")

    def read(self, address, length):
        buffer = ct.create_string_buffer(length)
        count = self.dll.JLINKARM_ReadMemEx(address, length, buffer, 0)
        if count != length:
            raise RuntimeError(f"Short read at 0x{address:08X}: {count}/{length}")
        return buffer.raw

    def read32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def write(self, address, data, width=1):
        buffer = ct.create_string_buffer(data)
        count = self.dll.JLINKARM_WriteMemEx(address, len(data), buffer, width)
        if count != len(data):
            raise RuntimeError(f"Short write at 0x{address:08X}: {count}/{len(data)}")

    def write32(self, address, value):
        self.write(address, struct.pack("<I", value), 4)

    def halted(self):
        return bool(self.check(self.dll.JLINKARM_IsHalted(), "query CPU"))

    def halt(self):
        if self.dll.JLINKARM_Halt() != 0:
            raise RuntimeError("Cannot halt CPU")
        deadline = time.monotonic() + 2
        while not self.halted():
            if time.monotonic() > deadline:
                raise RuntimeError("CPU halt timed out")
            time.sleep(0.01)

    def go(self):
        self.dll.JLINKARM_Go()

    def reset_run(self):
        self.reset_halt()
        self.go()

    def reset_halt(self):
        self.check(self.dll.JLINKARM_Reset(), "reset")
        self.halt()

    def clear_boot_request(self):
        if self.read32(0x40002850) != 1:
            return
        # Same backup-domain access sequence as boot_request_write().
        clock = self.read32(0x40023840)
        self.write32(0x40023840, clock | (1 << 28))
        control = self.read32(0x40007000)
        try:
            self.write32(0x40007000, control | (1 << 8))
            if not self.read32(0x40007000) & (1 << 8):
                raise RuntimeError("Cannot enable backup-domain access")
            self.write32(0x40002850, 0)
            if self.read32(0x40002850) != 0:
                raise RuntimeError("Cannot clear Bootloader request")
        finally:
            self.write32(0x40007000, control)
            self.write32(0x40023840, clock)

    def program_image(self, address, data):
        self.command("EnableFlashDL")
        buffer = ct.create_string_buffer(data)
        self.dll.JLINKARM_BeginDownload(0)
        count = self.dll.JLINKARM_WriteMem(address, len(data), buffer)
        result = self.dll.JLINKARM_EndDownload()
        self.command("DisableFlashDL")
        if count != len(data) or result < 0:
            raise RuntimeError(f"Image download failed: {count}/{len(data)}, result={result}")
        self.halt()

    def close(self):
        if self.opened:
            self.dll.JLINKARM_Close()
            self.opened = False
        self.log_file.close()


class Flash:
    """F407 sector erase and byte programming over SWD, with the CPU halted.

    Control sequences follow RM0090 and the project's STM32F4 HAL. Image data
    uses SEGGER's loader; the 6-byte header commit never invokes that loader.
    """

    def __init__(self, probe):
        self.probe = probe

    def ready(self, check_errors=True):
        deadline = time.monotonic() + 10
        while True:
            status = self.probe.read32(FLASH_SR)
            if not status & BUSY:
                if check_errors and status & ERRORS:
                    raise RuntimeError(f"Flash error status 0x{status:08X}")
                return
            if time.monotonic() > deadline:
                raise RuntimeError("Flash operation timed out")
            time.sleep(0.005)

    def unlock(self):
        if not self.probe.halted():
            raise RuntimeError("CPU must be halted before Flash operations")
        self.probe.command("DisableFlashDL")
        self.ready()
        if self.probe.read32(FLASH_CR) & LOCK:
            self.probe.write32(FLASH_KEYR, 0x45670123)
            self.probe.write32(FLASH_KEYR, 0xCDEF89AB)
        if self.probe.read32(FLASH_CR) & LOCK:
            raise RuntimeError("Flash remained locked")
        self.probe.write32(FLASH_SR, ERRORS | 1)

    def lock(self):
        self.ready(check_errors=False)
        self.probe.write32(FLASH_CR, LOCK)

    def flush(self):
        acr = self.probe.read32(FLASH_ACR)
        disabled = acr & ~0x600
        self.probe.write32(FLASH_ACR, disabled)
        self.probe.write32(FLASH_ACR, disabled | 0x1800)
        self.probe.write32(FLASH_ACR, disabled)
        self.probe.write32(FLASH_ACR, acr)
        self.probe.command("InvalidateCache")

    def erase(self, sector):
        if sector not in range(5, 11):
            raise ValueError("Only App sectors 5..10 may be erased")
        self.unlock()
        try:
            # x8 parallelism works across the supported supply range.
            self.probe.write32(FLASH_CR, 2 | (sector << 3))
            self.probe.write32(FLASH_CR, 2 | (sector << 3) | 0x10000)
            self.ready()
        finally:
            self.lock()
        self.flush()
        address = 0x08020000 + (sector - 5) * SECTOR_SIZE
        if self.probe.read(address, SECTOR_SIZE) != b"\xff" * SECTOR_SIZE:
            raise RuntimeError(f"Sector {sector} did not erase completely")

    def program_header(self, address, data):
        if not any(base <= address and address + len(data) <= base + 8 for base in SLOTS.values()):
            raise ValueError("Raw programming is restricted to the slot info")
        if self.probe.read(address, len(data)) != b"\xff" * len(data):
            raise RuntimeError("Header destination is not erased")
        self.unlock()
        try:
            self.probe.write32(FLASH_CR, 1)  # PG=1, PSIZE=x8; no SER/MER/STRT.
            for offset, value in enumerate(data):
                self.probe.write(address + offset, bytes([value]))
                self.ready()
        finally:
            self.lock()
        self.flush()
        if self.probe.read(address, len(data)) != data:
            raise RuntimeError("Header readback mismatch")


def check_device(probe):
    if probe.read32(0xE0042000) & 0xFFF != 0x413:
        raise RuntimeError("Expected STM32F405/407 device ID 0x413")
    if struct.unpack("<H", probe.read(0x1FFF7A22, 2))[0] != 1024:
        raise RuntimeError("Expected 1024 KiB Flash")


def download(probe, slot, firmware, folder):
    check_device(probe)
    validate_vectors(firmware, SLOTS[slot] + HEADER_SIZE)
    was_halted = probe.halted()
    touched = False
    probe.halt()
    try:
        before = probe.read(FLASH_BASE, FLASH_SIZE)
        (folder / "before.bin").write_bytes(before)
        headers = {s: before[a - FLASH_BASE:a - FLASH_BASE + 8] for s, a in SLOTS.items()}
        plan = plan_download(slot, firmware, headers)
        (folder / "plan.json").write_text(json.dumps(plan, indent=2), encoding="utf-8")
        print(f"Slot {slot}, generation {plan['generation']}, sectors {plan['sectors']}", flush=True)
        # A fresh hardware reset halts in Bootloader; it also clears watchdog/
        # peripheral state before the Flash loader temporarily uses target RAM.
        probe.reset_halt()
        if probe.read32(0x40023C14) & 0xFF00 != 0xAA00:
            raise RuntimeError("Read protection is enabled; refusing to change option bytes")
        protected = (probe.read32(0x40023C14) >> 16) & 0xFFF
        if any(not protected & (1 << s) for s in plan["sectors"]):
            raise RuntimeError("Target App sectors are write protected")
        flash = Flash(probe)
        for sector in plan["sectors"]:
            touched = True
            print(f"Erase sector {sector}", flush=True)
            flash.erase(sector)
        print(f"Program and verify {len(firmware)} bytes", flush=True)
        probe.program_image(plan["app_address"], firmware)
        flash.flush()
        after = probe.read(FLASH_BASE, FLASH_SIZE)
        base = SLOTS[slot] - FLASH_BASE
        stop = base + len(plan["sectors"]) * SECTOR_SIZE
        expected = bytearray(before)
        expected[base:stop] = b"\xff" * (stop - base)
        expected[base + HEADER_SIZE:base + HEADER_SIZE + len(firmware)] = firmware
        if after != expected:
            (folder / "failed_readback.bin").write_bytes(after)
            raise RuntimeError("Full Flash verification failed; slot header was NOT committed")
        header = bytes.fromhex(plan["header"])
        print("Image verified; initialize slot info, then commit write_done", flush=True)
        flash.program_header(SLOTS[slot], header[:5])
        flash.program_header(SLOTS[slot] + 5, header[5:6])
        expected[base:base + 8] = header
        after = probe.read(FLASH_BASE, FLASH_SIZE)
        if after != expected:
            raise RuntimeError("Final Flash verification failed")
        (folder / "after.bin").write_bytes(after)
        probe.clear_boot_request()
        probe.reset_run()
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            current = probe.read(SLOTS[slot], 8)
            vtor = probe.read32(0xE000ED08)
            if slot_state(current) == "confirmed" and vtor == plan["app_address"]:
                result = dict(plan, final_header=current.hex(), vtor=f"0x{vtor:08X}",
                              before_sha256=hashlib.sha256(before).hexdigest(),
                              programmed_sha256=hashlib.sha256(after).hexdigest(), confirmed=True)
                (folder / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
                print(f"SUCCESS: App {slot} running and confirmed, VTOR=0x{vtor:08X}", flush=True)
                return result
            time.sleep(0.1)
        raise RuntimeError(f"Image written, but App {slot} did not confirm within 8 seconds; inspect the board")
    except BaseException:
        if not touched and not was_halted and probe.halted():
            probe.go()
        raise


def default_serial(slot):
    options = ROOT / "app/MDK-ARM/modbus-gateway-stm32.uvoptx"
    for target in ET.parse(options).getroot().findall("Target"):
        if target.findtext("TargetName") == "app_" + slot:
            for entry in target.findall(".//TargetDriverDllRegistry/SetRegEntry"):
                if entry.findtext("Key") == "JL2CM3":
                    match = re.search(r"(?:^|\s)-U(\d+)(?:\s|$)", entry.findtext("Name", ""))
                    if match:
                        return int(match[1])
    raise ValueError("Specify --serial, or select a J-Link serial number in the Keil target settings")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--slot", choices=SLOTS, required=True)
    parser.add_argument("--image", type=Path, required=True, help="App-only HEX produced by the selected target")
    parser.add_argument("--serial", type=int)
    parser.add_argument("--dll", type=Path)
    parser.add_argument("--check", action="store_true", help="Validate HEX only, without connecting to hardware")
    parser.add_argument("--inspect", action="store_true", help="Read chip/slot state only, without halting or writing")
    args = parser.parse_args()
    folder = ROOT / "artifacts/keil-slot-download" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    folder.mkdir(parents=True)
    probe = None
    try:
        firmware = hex_to_bin(args.image.resolve(), SLOTS[args.slot] + HEADER_SIZE)
        validate_vectors(firmware, SLOTS[args.slot] + HEADER_SIZE)
        print(f"App {args.slot}: {len(firmware)} bytes, SHA256 {hashlib.sha256(firmware).hexdigest()}", flush=True)
        if args.check:
            return 0
        dll = args.dll
        if dll is None:
            commander = shutil.which("JLink.exe")
            if not commander:
                raise ValueError("JLink.exe must be on PATH, or specify --dll")
            dll = Path(commander).with_name("JLink_x64.dll" if ct.sizeof(ct.c_void_p) == 8 else "JLinkARM.dll")
        probe = JLink(dll, args.serial or default_serial(args.slot), folder)
        if args.inspect:
            check_device(probe)
            for slot, address in SLOTS.items():
                header = probe.read(address, 8)
                print(f"Slot {slot}: {header.hex(' ')} ({slot_state(header)})")
            print(f"VTOR=0x{probe.read32(0xE000ED08):08X}, halted={probe.halted()}")
        else:
            download(probe, args.slot, firmware, folder)
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        (folder / "error.txt").write_text(str(error), encoding="utf-8")
        print(f"ERROR: {error}", file=sys.stderr, flush=True)
        return 1
    finally:
        if probe:
            probe.close()
        print(f"Evidence: {folder}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
