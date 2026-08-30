"""Regenerate Configuration API certificate fixtures.

The script writes certificates only. Private keys exist only in process memory and
are never written to the repository. The generated certificate bytes are expected
to change when this script is rerun; the checked-in outputs are the fixed fixtures
used by the tests.
"""

from __future__ import annotations

import base64
from datetime import datetime, timezone
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ed25519, ec, rsa
from cryptography.x509.oid import NameOID


OUTPUT_DIRECTORY = Path(__file__).resolve().parent


def name(common_name: str) -> x509.Name:
    return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, common_name)])


def key_usage(key_cert_sign: bool) -> x509.KeyUsage:
    return x509.KeyUsage(
        digital_signature=not key_cert_sign,
        content_commitment=False,
        key_encipherment=False,
        data_encipherment=False,
        key_agreement=False,
        key_cert_sign=key_cert_sign,
        crl_sign=key_cert_sign,
        encipher_only=None,
        decipher_only=None,
    )


def certificate(
    *,
    subject_key: rsa.RSAPrivateKey | ec.EllipticCurvePrivateKey,
    subject: x509.Name,
    issuer_key: rsa.RSAPrivateKey | ec.EllipticCurvePrivateKey,
    issuer: x509.Name,
    serial_number: int,
    not_before: datetime,
    not_after: datetime,
    is_ca: bool,
    include_key_usage: bool = True,
    key_cert_sign: bool = True,
) -> x509.Certificate:
    builder = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(subject_key.public_key())
        .serial_number(serial_number)
        .not_valid_before(not_before)
        .not_valid_after(not_after)
        .add_extension(x509.BasicConstraints(ca=is_ca, path_length=None), critical=True)
    )
    if include_key_usage:
        builder = builder.add_extension(key_usage(key_cert_sign), critical=True)
    return builder.sign(private_key=issuer_key, algorithm=hashes.SHA256())


def self_signed_rsa(
    common_name: str,
    serial_number: int,
    not_before: datetime,
    not_after: datetime,
    *,
    is_ca: bool = True,
    include_key_usage: bool = True,
    key_cert_sign: bool = True,
) -> tuple[x509.Certificate, rsa.RSAPrivateKey]:
    private_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = name(common_name)
    return (
        certificate(
            subject_key=private_key,
            subject=subject,
            issuer_key=private_key,
            issuer=subject,
            serial_number=serial_number,
            not_before=not_before,
            not_after=not_after,
            is_ca=is_ca,
            include_key_usage=include_key_usage,
            key_cert_sign=key_cert_sign,
        ),
        private_key,
    )


def write_pem(filename: str, value: x509.Certificate) -> bytes:
    pem = value.public_bytes(serialization.Encoding.PEM)
    (OUTPUT_DIRECTORY / filename).write_bytes(pem)
    return pem


def pem_from_der(der: bytes) -> bytes:
    encoded = base64.b64encode(der)
    lines = [encoded[index : index + 64] for index in range(0, len(encoded), 64)]
    return b"-----BEGIN CERTIFICATE-----\n" + b"\n".join(lines) + b"\n-----END CERTIFICATE-----\n"


def main() -> None:
    old = datetime(2010, 1, 1, tzinfo=timezone.utc)
    expired = datetime(2011, 1, 1, tzinfo=timezone.utc)
    current_start = datetime(2020, 1, 1, tzinfo=timezone.utc)
    current_end = datetime(2040, 1, 1, tzinfo=timezone.utc)
    future_start = datetime(2035, 1, 1, tzinfo=timezone.utc)
    future_end = datetime(2045, 1, 1, tzinfo=timezone.utc)

    valid_root, valid_root_key = self_signed_rsa(
        "Configuration Test Root", 1001, current_start, current_end
    )
    valid_root_pem = write_pem("valid_root.pem", valid_root)
    (OUTPUT_DIRECTORY / "valid_root.der").write_bytes(valid_root.public_bytes(serialization.Encoding.DER))
    (OUTPUT_DIRECTORY / "valid_root_crlf.pem").write_bytes(valid_root_pem.replace(b"\n", b"\r\n"))

    alternate_root, _ = self_signed_rsa(
        "Configuration Alternate Root", 1002, current_start, current_end
    )
    alternate_root_pem = write_pem("alternate_root.pem", alternate_root)

    no_key_usage, _ = self_signed_rsa(
        "Configuration Root Without Key Usage",
        1003,
        current_start,
        current_end,
        include_key_usage=False,
    )
    write_pem("valid_root_no_key_usage.pem", no_key_usage)

    expired_root, _ = self_signed_rsa("Configuration Expired Root", 1004, old, expired)
    write_pem("expired_root.pem", expired_root)

    future_root, _ = self_signed_rsa(
        "Configuration Future Root", 1005, future_start, future_end
    )
    write_pem("future_root.pem", future_root)

    not_ca, _ = self_signed_rsa(
        "Configuration End Entity", 1006, current_start, current_end, is_ca=False, key_cert_sign=False
    )
    write_pem("not_ca.pem", not_ca)

    wrong_key_usage, _ = self_signed_rsa(
        "Configuration Wrong Key Usage",
        1007,
        current_start,
        current_end,
        key_cert_sign=False,
    )
    write_pem("wrong_key_usage.pem", wrong_key_usage)

    intermediate_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    intermediate = certificate(
        subject_key=intermediate_key,
        subject=name("Configuration Intermediate CA"),
        issuer_key=valid_root_key,
        issuer=valid_root.subject,
        serial_number=1008,
        not_before=current_start,
        not_after=current_end,
        is_ca=True,
    )
    write_pem("intermediate_ca.pem", intermediate)

    cross_signer, cross_signer_key = self_signed_rsa(
        "Configuration Cross Signer", 1009, current_start, current_end
    )
    cross_signed_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    cross_signed = certificate(
        subject_key=cross_signed_key,
        subject=name("Configuration Cross Signed CA"),
        issuer_key=cross_signer_key,
        issuer=cross_signer.subject,
        serial_number=1010,
        not_before=current_start,
        not_after=current_end,
        is_ca=True,
    )
    write_pem("cross_signed_ca.pem", cross_signed)

    bad_signature_der = bytearray(valid_root.public_bytes(serialization.Encoding.DER))
    bad_signature_der[-1] ^= 0x01
    (OUTPUT_DIRECTORY / "bad_self_signature.pem").write_bytes(pem_from_der(bytes(bad_signature_der)))

    ecdsa_key = ec.generate_private_key(ec.SECP256R1())
    ecdsa_subject = name("Configuration ECDSA Root")
    ecdsa_root = certificate(
        subject_key=ecdsa_key,
        subject=ecdsa_subject,
        issuer_key=ecdsa_key,
        issuer=ecdsa_subject,
        serial_number=1011,
        not_before=current_start,
        not_after=current_end,
        is_ca=True,
    )
    write_pem("valid_ecdsa_root.pem", ecdsa_root)

    unsupported_key = ed25519.Ed25519PrivateKey.generate()
    unsupported_subject = name("Configuration Unsupported Algorithm Root")
    unsupported_root = (
        x509.CertificateBuilder()
        .subject_name(unsupported_subject)
        .issuer_name(unsupported_subject)
        .public_key(unsupported_key.public_key())
        .serial_number(1012)
        .not_valid_before(current_start)
        .not_valid_after(current_end)
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(key_usage(True), critical=True)
        .sign(private_key=unsupported_key, algorithm=None)
    )
    write_pem("unsupported_algorithm.pem", unsupported_root)

    (OUTPUT_DIRECTORY / "bundle.pem").write_bytes(valid_root_pem + alternate_root_pem)
    (OUTPUT_DIRECTORY / "malformed.pem").write_bytes(
        b"-----BEGIN CERTIFICATE-----\nnot-base64!\n-----END CERTIFICATE-----\n"
    )


if __name__ == "__main__":
    main()
