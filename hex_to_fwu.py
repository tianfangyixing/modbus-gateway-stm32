#!/usr/bin/env python3
"""将 Intel HEX 转换为 BIN，再生成 AES-128-GCM 加密的 FU 固件包。

本脚本可单文件复制运行，无需其他项目 Python 文件。
需要 Python 3.10+，安装依赖：python -m pip install cryptography
默认双镜像：python hex_to_fwu.py
单镜像：python hex_to_fwu.py app.hex
本项目：python hex_to_fwu.py app/MDK-ARM/app_A/app_A.hex --slot A
指定输出：python hex_to_fwu.py app.hex -o output/app.fu
双镜像：python hex_to_fwu.py app_A.hex app_B.hex -o output/app.fu
省略输入时读取当前目录 app_A.hex、app_B.hex，默认生成当前目录 app.fu。
显式指定单输入生成 FU01，默认输出输入旁同名 .bin/.fu。
双输入按顺序代表 A、B，生成 FU02；默认输出当前目录 app.fu、app_A.bin、app_B.bin。
双输入不接受 --slot；显式输出 release.fu/.fwu 时，BIN 为 release_A.bin/release_B.bin。
运行时输入 32 个十六进制字符的密钥（16 字节），输入回显，必须与设备端 key 一致。

BIN 从 APP_ADDRESS 开始，到 HEX 最后一个数据字节结束，空洞填 0xFF。
长度必须在 [1024, 392704] 字节内；不自动补足最小长度，不进行 AES 块填充。
FU01 文件布局（N 为 BIN 大小，也等于纯密文大小；总长度 N + 68）：
    偏移 0       : MAGIC（4 字节，ASCII FU01）
    偏移 4       : 随机 nonce（12 字节）
    偏移 16      : tag（16 字节）
    偏移 32      : N（4 字节，小端）
    偏移 36      : SHA256（32 字节，密文的原始摘要）
    偏移 68      : 密文（N 字节）
FU02 文件头为 <4sI12s16sI12s16s32s（100 字节）：
    FU02 + A size/nonce/tag + B size/nonce/tag + SHA256 + A 密文 + B 密文
A/B 长度独立，nonce 不同；SHA256 严格覆盖 data[:68] + data[100:]。
两种格式 GCM 的 AAD 均为空。FU02 仅供离线打包验证，现有发送器仍使用 FU01。

参考：
https://www.keil.com/support/docs/1584/_hlp_hexfile.htm
https://cryptography.io/en/latest/hazmat/primitives/aead/#cryptography.hazmat.primitives.ciphers.aead.AESGCM
"""

import argparse
from dataclasses import dataclass
import hashlib
import hmac
import os
from pathlib import Path
import re
import struct
import sys
import tempfile


# 独立定义 FU01 文件头以支持单文件运行，布局与 fwu_file.py 保持一致。
FILE_MAGIC = b"FU01"
FILE_HEADER = struct.Struct("<4s12s16sI32s")
FU02_MAGIC = b"FU02"
FU02_PREFIX = struct.Struct("<4sI12s16sI12s16s")
FU02_HEADER = struct.Struct("<4sI12s16sI12s16s32s")


# 与 bootloader/Core/Src/bootloader.c 保持一致。
SLOT_ADDRESSES = {"A": 0x08020200, "B": 0x08080200}
APP_ADDRESS = SLOT_ADDRESSES["A"]
MIN_SIZE = 1024
MAX_SIZE = 0x5FE00
NONCE_SIZE = 12
TAG_SIZE = 16


def validate_size(size: int) -> None:
    if not MIN_SIZE <= size <= MAX_SIZE:
        raise ValueError(
            f"BIN 大小为 {size} 字节，必须在 {MIN_SIZE}～{MAX_SIZE} 字节之间（含边界）"
        )


def hex_to_bin(path: Path, app_address: int = APP_ADDRESS) -> bytes:
    """校验 HEX 记录及地址，返回 APP 分区内的连续二进制镜像。"""
    if app_address not in SLOT_ADDRESSES.values():
        raise ValueError("APP address must match slot A or B")
    firmware = bytearray(b"\xff") * MAX_SIZE
    written = bytearray(MAX_SIZE)
    base = 0
    first = MAX_SIZE
    end = 0
    eof = False

    with path.open("r", encoding="utf-8-sig") as source:
        for line_number, line in enumerate(source, 1):
            line = line.strip()
            if not line:
                continue

            def fail(message: str) -> None:
                raise ValueError(f"HEX 第 {line_number} 行：{message}")

            if eof:
                fail("EOF 记录后仍有内容")
            if not re.fullmatch(r":[0-9a-fA-F]{10,520}", line) or len(line) % 2 != 1:
                fail("记录格式错误，应为冒号后跟完整的十六进制字节")

            record = bytes.fromhex(line[1:])
            count = record[0]
            address = int.from_bytes(record[1:3], "big")
            kind = record[3]
            data = record[4:-1]
            if len(data) != count:
                fail("数据长度与记录声明不一致")
            if sum(record) & 0xFF:
                fail("校验和错误")

            if kind == 0x00:
                if not count:
                    continue
                start = base + address - app_address
                stop = start + count
                if start < 0 or stop > MAX_SIZE:
                    fail(
                        f"数据地址 0x{base + address:08X}～"
                        f"0x{base + address + count - 1:08X} 超出 APP 分区 "
                        f"[0x{app_address:08X}, 0x{app_address + MAX_SIZE - 1:08X}]"
                    )
                if any(written[start:stop]):
                    fail("数据地址重叠")
                firmware[start:stop] = data
                written[start:stop] = b"\x01" * count
                first = min(first, start)
                end = max(end, stop)
            elif kind == 0x01:
                if count != 0 or address != 0:
                    fail("EOF 记录的数据长度和地址字段必须为 0")
                eof = True
            elif kind in (0x02, 0x04):
                if count != 2 or address != 0:
                    fail("扩展地址记录必须含 2 字节数据，地址字段必须为 0")
                base = int.from_bytes(data, "big") << (4 if kind == 0x02 else 16)
            elif kind in (0x03, 0x05):
                if count != 4 or address != 0:
                    fail("启动地址记录必须含 4 字节数据，地址字段必须为 0")
                # 启动地址是元数据，不写入 BIN。设备从 APP 向量表读取复位入口。
            else:
                fail(f"不支持的记录类型 0x{kind:02X}")

    if not eof:
        raise ValueError("HEX 缺少 EOF 结束记录，文件可能不完整")
    if not end:
        raise ValueError("HEX 没有固件数据")
    if first != 0:
        raise ValueError(
            f"HEX 首地址为 0x{app_address + first:08X}，"
            f"必须为 bootloader 的 app_address 0x{app_address:08X}"
        )
    validate_size(end)
    return bytes(firmware[:end])


def parse_key(text: str) -> bytes:
    """接受连续或以空白分隔的十六进制字节，不对口令做隐式派生。"""
    compact = "".join(text.split())
    if not re.fullmatch(r"[0-9a-fA-F]{32}", compact):
        raise ValueError("密钥必须是 32 个十六进制字符（16 字节，可含空格）")
    return bytes.fromhex(compact)


def validate_vectors(firmware: bytes, app_address: int) -> None:
    """FU02 镜像须包含本槽内的 Thumb 复位入口及合法、8 字节对齐的栈顶。"""
    validate_size(len(firmware))
    if app_address not in SLOT_ADDRESSES.values():
        raise ValueError("APP address must match slot A or B")
    stack, reset = struct.unpack_from("<II", firmware)
    if stack % 8 or not (
        0x20000000 < stack <= 0x20020000 or 0x10000000 < stack <= 0x10010000
    ):
        raise ValueError("初始栈指针必须 8 字节对齐，且位于 SRAM/CCM 允许范围内")
    entry = reset & ~1
    if not (reset & 1) or not app_address + 8 <= entry < app_address + len(firmware):
        raise ValueError("复位向量必须为 Thumb 入口，且位于本镜像起始地址 + 8 到镜像末端之前")


def aesgcm_for_key(key: bytes):
    """延迟加载加密依赖；无需密钥的 FU02 读取仅使用标准库。"""
    if len(key) != 16:
        raise ValueError("AES-GCM-128 密钥长度必须为 16 字节")
    try:
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    except ImportError as exc:
        raise ValueError(
            "缺少 cryptography 依赖，请执行：python -m pip install cryptography"
        ) from exc
    return AESGCM(key)


def bin_to_fwu(firmware: bytes, key: bytes) -> bytes:
    """生成 MAGIC + nonce + tag + size + SHA256(ciphertext) + ciphertext。"""
    validate_size(len(firmware))
    aesgcm = aesgcm_for_key(key)
    nonce = os.urandom(NONCE_SIZE)
    # AESGCM.encrypt 返回 ciphertext || tag；AAD 为空，不添加 PKCS#7 填充。
    encrypted = aesgcm.encrypt(nonce, firmware, None)
    ciphertext, tag = encrypted[:-TAG_SIZE], encrypted[-TAG_SIZE:]
    return FILE_HEADER.pack(FILE_MAGIC, nonce, tag, len(ciphertext),
                            hashlib.sha256(ciphertext).digest()) + ciphertext


@dataclass(frozen=True)
class FU02Image:
    nonce: bytes
    tag: bytes
    ciphertext: bytes


def parse_fu02(data: bytes) -> tuple[FU02Image, FU02Image]:
    """无密钥校验整个 FU02 后返回 A、B；不代表已通过 GCM 认证。"""
    if len(data) < FU02_HEADER.size:
        raise ValueError("FU02 文件头不足 100 字节")
    magic, a_size, a_nonce, a_tag, b_size, b_nonce, b_tag, digest = FU02_HEADER.unpack_from(data)
    if magic != FU02_MAGIC:
        raise ValueError("文件 magic 必须为 FU02")
    for slot, size in (("A", a_size), ("B", b_size)):
        if not MIN_SIZE <= size <= MAX_SIZE:
            raise ValueError(f"FU02 {slot} 大小必须为 {MIN_SIZE}～{MAX_SIZE} 字节")
    if len(data) != FU02_HEADER.size + a_size + b_size:
        raise ValueError("FU02 文件总长度必须等于 100 + a_size + b_size")
    if a_nonce == b_nonce:
        raise ValueError("FU02 A/B nonce 不能相同")
    calculated = hashlib.sha256(data[:FU02_PREFIX.size])
    calculated.update(data[FU02_HEADER.size:])
    if not hmac.compare_digest(calculated.digest(), digest):
        raise ValueError("FU02 整包 SHA256 不匹配")
    b_offset = FU02_HEADER.size + a_size
    return (
        FU02Image(a_nonce, a_tag, data[FU02_HEADER.size:b_offset]),
        FU02Image(b_nonce, b_tag, data[b_offset:]),
    )


def read_fu02(path: Path) -> tuple[FU02Image, FU02Image]:
    """有界读取 FU02 并验证结构、nonce 和整包摘要。"""
    with path.open("rb") as stream:
        data = stream.read(FU02_HEADER.size + 2 * MAX_SIZE + 1)
    return parse_fu02(data)


def decrypt_fu02(data: bytes, key: bytes) -> tuple[bytes, bytes]:
    """先校验整包，再分别认证、解密 A/B；任一失败都不返回明文。"""
    images = parse_fu02(data)
    aesgcm = aesgcm_for_key(key)
    from cryptography.exceptions import InvalidTag

    plaintext = []
    for slot, image in zip(("A", "B"), images):
        try:
            plaintext.append(aesgcm.decrypt(image.nonce, image.ciphertext + image.tag, None))
        except InvalidTag as exc:
            raise ValueError(f"FU02 {slot} 镜像 GCM 认证失败") from exc
    return plaintext[0], plaintext[1]


def bins_to_fu02(firmware_a: bytes, firmware_b: bytes, key: bytes) -> bytes:
    """独立加密 A/B，并在返回前验证整包及两份解密结果。"""
    for slot, firmware in (("A", firmware_a), ("B", firmware_b)):
        try:
            validate_vectors(firmware, SLOT_ADDRESSES[slot])
        except ValueError as exc:
            raise ValueError(f"{slot} 镜像：{exc}") from exc
    aesgcm = aesgcm_for_key(key)
    a_nonce, b_nonce = os.urandom(NONCE_SIZE), os.urandom(NONCE_SIZE)
    if len(a_nonce) != NONCE_SIZE or len(b_nonce) != NONCE_SIZE:
        raise ValueError("随机源必须提供 12 字节 nonce")
    if a_nonce == b_nonce:
        raise ValueError("A/B 随机 nonce 碰撞，请重新打包")
    encrypted_a = aesgcm.encrypt(a_nonce, firmware_a, None)
    encrypted_b = aesgcm.encrypt(b_nonce, firmware_b, None)
    ciphertext_a, a_tag = encrypted_a[:-TAG_SIZE], encrypted_a[-TAG_SIZE:]
    ciphertext_b, b_tag = encrypted_b[:-TAG_SIZE], encrypted_b[-TAG_SIZE:]
    prefix = FU02_PREFIX.pack(
        FU02_MAGIC, len(ciphertext_a), a_nonce, a_tag,
        len(ciphertext_b), b_nonce, b_tag,
    )
    digest = hashlib.sha256(prefix)
    digest.update(ciphertext_a)
    digest.update(ciphertext_b)
    package = prefix + digest.digest() + ciphertext_a + ciphertext_b
    if decrypt_fu02(package, key) != (firmware_a, firmware_b):
        raise ValueError("FU02 解密回读与输入 BIN 不一致")
    return package


def validate_paths(inputs: tuple[Path, ...], outputs: tuple[Path, ...]) -> None:
    """路径已规范化；进一步拒绝硬链接别名和无法作为文件输出的路径。"""
    paths = inputs + outputs
    for i, left in enumerate(paths):
        for right in paths[i + 1:]:
            if left == right or (left.exists() and right.exists() and left.samefile(right)):
                raise ValueError("输入 HEX 和所有输出 BIN/FU 必须是不同文件")
    for path in outputs:
        if path.exists() and not path.is_file():
            raise ValueError(f"输出路径不是普通文件：{path}")
        parent = path.parent
        while not parent.exists():
            parent = parent.parent
        if not parent.is_dir():
            raise ValueError(f"输出父路径不是目录：{parent}")


def write_atomic(path: Path, content: bytes) -> None:
    """先写同目录临时文件，避免中断时留下半个 BIN/FWU 文件。"""
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".fwu-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", nargs="?", type=Path,
                        help="单输入 HEX 或双输入的 A HEX；省略输入时使用 app_A.hex 和 app_B.hex")
    parser.add_argument("input_b", nargs="?", type=Path, help="双输入的 B HEX；与第一个输入生成 FU02")
    parser.add_argument("-o", "--output", type=Path,
                        help="输出 .fu/.fwu；单输入默认输入旁同名 .fu，双输入默认当前目录 app.fu")
    parser.add_argument("--slot", choices=("A", "B"), help="仅单输入可用，目标链接槽（默认 A）")
    args = parser.parse_args(argv)
    if args.input is None:
        args.input = Path("app_A.hex")
        args.input_b = Path("app_B.hex")
    dual = args.input_b is not None
    if dual and args.slot is not None:
        parser.error("双输入 FU02 模式不接受 --slot；参数顺序固定为 A、B")

    try:
        input_args = (args.input, args.input_b) if dual else (args.input,)
        inputs = tuple(path.resolve() for path in input_args)
        for path in inputs:
            if path.suffix.lower() != ".hex":
                raise ValueError("输入文件扩展名必须为 .hex")
        output_path = args.output or (Path("app.fu") if dual else args.input.with_suffix(".fu"))
        if not dual:
            output_path = output_path.resolve()  # 保留 FU01 的既有路径行为。
        if output_path.suffix.lower() not in (".fu", ".fwu"):
            raise ValueError("输出文件扩展名必须为 .fu 或 .fwu")
        if dual:
            bin_paths = tuple(
                output_path.with_name(output_path.stem + f"_{slot}.bin").resolve()
                for slot in ("A", "B")
            )
        else:
            bin_paths = (output_path.with_suffix(".bin"),)
        output_path = output_path.resolve()
        validate_paths(inputs, bin_paths + (output_path,))
        slots = ("A", "B") if dual else (args.slot or "A",)
        firmwares = []
        for slot, input_path in zip(slots, inputs):
            try:
                firmware = hex_to_bin(input_path, SLOT_ADDRESSES[slot])
                if dual:
                    validate_vectors(firmware, SLOT_ADDRESSES[slot])
            except (OSError, ValueError) as exc:
                raise ValueError(f"{slot} HEX {input_path}：{exc}") from exc
            firmwares.append(firmware)
            print(f"{slot} BIN 大小：{len(firmware)} 字节，起始地址：0x{SLOT_ADDRESSES[slot]:08X}")
        key = parse_key(input("请输入 AES-128 密钥（32 个十六进制字符，输入回显）："))
        package = bins_to_fu02(*firmwares, key) if dual else bin_to_fwu(firmwares[0], key)
        # 所有校验和双镜像解密回读完成后才开始写文件；FU 最后提交。
        for path in bin_paths + (output_path,):
            path.parent.mkdir(parents=True, exist_ok=True)
        for bin_path, firmware in zip(bin_paths, firmwares):
            write_atomic(bin_path, firmware)
        write_atomic(output_path, package)
        for slot, bin_path, firmware in zip(slots, bin_paths, firmwares):
            print(f"已生成 {slot} BIN：{bin_path}（{len(firmware)} 字节）")
        print(f"已生成 {'FU02' if dual else 'FU01'}：{output_path}（{len(package)} 字节）")
        return 0
    except (OSError, ValueError) as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 1
    except (EOFError, KeyboardInterrupt):
        print("\n已取消：未完成密钥输入。", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
