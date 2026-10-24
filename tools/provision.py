#!/usr/bin/env python3
"""Write a trust anchor (public key) into the emulated ESP32's "keys" partition.

Usage:
  provision.py <public_key.pem>       provision from an OpenSSL public key
  provision.py --record <file>        reapply a saved record (used by reset_device.sh)

Supports ECDSA P-256 and ML-DSA-44 public keys in SubjectPublicKeyInfo PEM format,
as produced by `openssl pkey -pubout`. Standard library only.
"""
import base64
import os
import struct
import sys

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FLASH = os.path.join(LAB, "device", "flash_image.bin")
SAVED = os.path.join(LAB, "keys", ".provisioned")

PT_OFFSET = 0x8000
PT_MAGIC = b"\xaa\x50"

TA_MAGIC = b"PQTA"
TYPE_ECDSA_P256 = 1
TYPE_MLDSA44 = 2
NAMES = {TYPE_ECDSA_P256: "ECDSA-P256", TYPE_MLDSA44: "ML-DSA-44"}

OID_EC_PUBLIC_KEY = bytes.fromhex("2a8648ce3d0201")
OID_PRIME256V1 = bytes.fromhex("2a8648ce3d030107")
OID_ML_DSA_44 = bytes.fromhex("608648016503040311")


def fail(msg):
    print(f"Error: {msg}", file=sys.stderr)
    sys.exit(1)


def der_read(buf, pos):
    """Return (tag, value, next_pos) for the DER element at pos."""
    tag = buf[pos]
    length = buf[pos + 1]
    pos += 2
    if length & 0x80:
        n = length & 0x7F
        length = int.from_bytes(buf[pos:pos + n], "big")
        pos += n
    return tag, buf[pos:pos + length], pos + length


def parse_pem(path):
    try:
        text = open(path, "r", encoding="ascii").read()
    except FileNotFoundError:
        fail(f"{path} not found")
    except UnicodeDecodeError:
        fail(f"{path} is not a PEM file")
    if "PRIVATE KEY" in text:
        fail("that is a private key. Provision the public key (the _pub.pem file)")
    if "-----BEGIN PUBLIC KEY-----" not in text:
        fail(f"{path} is not a PEM public key")
    body = text.split("-----BEGIN PUBLIC KEY-----")[1].split("-----END PUBLIC KEY-----")[0]
    der = base64.b64decode("".join(body.split()))

    tag, spki, _ = der_read(der, 0)
    if tag != 0x30:
        fail("malformed public key")
    tag, algid, pos = der_read(spki, 0)
    tag2, bitstring, _ = der_read(spki, pos)
    if tag != 0x30 or tag2 != 0x03 or bitstring[0] != 0:
        fail("malformed public key")
    key = bitstring[1:]

    _, oid, apos = der_read(algid, 0)
    if oid == OID_EC_PUBLIC_KEY:
        _, curve, _ = der_read(algid, apos)
        if curve != OID_PRIME256V1:
            fail("only the P-256 curve is supported")
        if len(key) != 65 or key[0] != 0x04:
            fail("expected an uncompressed P-256 public key")
        return TYPE_ECDSA_P256, key[1:]
    if oid == OID_ML_DSA_44:
        if len(key) != 1312:
            fail("unexpected ML-DSA-44 public key length")
        return TYPE_MLDSA44, key
    fail("unsupported key type. Use an ECDSA P-256 or ML-DSA-44 public key")


def find_keys_partition(flash):
    for i in range(0, 0x0C00, 32):
        entry = flash[PT_OFFSET + i:PT_OFFSET + i + 32]
        if entry[:2] != PT_MAGIC:
            break
        offset, size = struct.unpack_from("<II", entry, 4)
        label = entry[12:28].rstrip(b"\x00").decode()
        if label == "keys":
            return offset, size
    fail("device flash image has no 'keys' partition. Run ./reset_device.sh")


def write_record(record):
    if not os.path.exists(FLASH):
        fail(f"{FLASH} not found")
    with open(FLASH, "r+b") as f:
        flash = f.read()
        offset, size = find_keys_partition(flash)
        if len(record) > size:
            fail("key does not fit in the keys partition")
        f.seek(offset)
        f.write(record + b"\xff" * (size - len(record)))


def main():
    args = sys.argv[1:]
    if len(args) == 2 and args[0] == "--record":
        record = open(args[1], "rb").read()
        if record[:4] != TA_MAGIC:
            fail("saved record is corrupt. Provision your public key again")
        write_record(record)
        print(f"Trust anchor kept: {NAMES.get(record[4], 'unknown')}")
        return record[4]
    if len(args) != 1 or args[0].startswith("-"):
        print(__doc__.strip())
        sys.exit(2)

    key_type, key = parse_pem(args[0])
    record = TA_MAGIC + struct.pack("<BH", key_type, len(key)) + key
    write_record(record)
    os.makedirs(os.path.dirname(SAVED), exist_ok=True)
    with open(SAVED, "wb") as f:
        f.write(record)
    print(f"Provisioned {NAMES[key_type]} trust anchor ({len(key)} bytes) into device key partition")
    return key_type


if __name__ == "__main__":
    main()
