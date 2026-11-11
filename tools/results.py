#!/usr/bin/env python3
"""Fill in the workshop answer sheet from the lab files.

Public key sizes come from keys/*_pub.pem. Signature size, verify time and the
install and tamper results come from device/device.log, which ./run_device.sh
writes on every boot. Standard library only.
"""
import base64
import os
import re

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOG = os.path.join(LAB, "device", "device.log")
SCHEMES = [("ECDSA P-256", "ECDSA-P256", "ecdsa_pub.pem"),
           ("ML-DSA-44", "ML-DSA-44", "mldsa_pub.pem")]

ANSI = re.compile(r"\x1b\[[0-9;]*m")
YEL, GRN, DIM, BOLD, OFF = "\033[30;43m", "\033[1;32m", "\033[2m", "\033[1m", "\033[0m"


def der_size(pem):
    path = os.path.join(LAB, "keys", pem)
    if not os.path.exists(path):
        return None
    text = open(path, encoding="ascii", errors="ignore").read()
    if "-----BEGIN PUBLIC KEY-----" not in text:
        return None
    body = text.split("-----BEGIN PUBLIC KEY-----")[1].split("-----END PUBLIC KEY-----")[0]
    return len(base64.b64decode("".join(body.split())))


def read_log():
    found = {tag: {} for _, tag, _ in SCHEMES}
    if not os.path.exists(LOG):
        return found
    current = None
    for raw in open(LOG, encoding="utf-8", errors="ignore"):
        line = ANSI.sub("", raw).strip()
        m = re.search(r"\[SIG \] (\S+) signature (\d+) bytes", line)
        if m and m.group(1) in found:
            current = m.group(1)
            found[current]["sig"] = int(m.group(2))
            continue
        if current is None:
            continue
        m = re.search(r"Verify: PASS in (\d+) ms", line)
        if m:
            found[current]["time"] = int(m.group(1))
            found[current]["installed"] = "Y"
        elif "Verify: FAIL" in line:
            found[current]["rejected"] = "Y"
    return found


def main():
    log = read_log()
    rows = [("Public key (DER bytes)", lambda s, d: der_size(s[2])),
            ("Signature (bytes)", lambda s, d: d.get("sig")),
            ("Device verify time (ms, emulated)", lambda s, d: d.get("time")),
            ("Valid update installed (Y/N)", lambda s, d: d.get("installed")),
            ("Tampered update rejected (Y/N)", lambda s, d: d.get("rejected"))]
    w0, w = 35, 14
    missing = False
    print()
    print(f"{BOLD}  YOUR ANSWER SHEET{OFF}")
    print(f"  {'Metric':<{w0}}{BOLD}{'ECDSA P-256':<{w}}{'ML-DSA-44':<{w}}{OFF}")
    print("  " + "-" * (w0 + 2 * w))
    for name, get in rows:
        cells = []
        for s in SCHEMES:
            v = get(s, log[s[1]])
            if v is None:
                missing = True
                cells.append(f"{DIM}{'not yet':<{w}}{OFF}")
            else:
                text = f"{v:,}" if isinstance(v, int) else v
                cells.append(f"{YEL} {text:<{w - 2}}{OFF} ")
        print(f"  {name:<{w0}}" + "".join(cells))
    print()
    if missing:
        print(f"  {DIM}'not yet' means that lab step has not been run. Finish it, then run ./results.sh again.{OFF}")
    else:
        print(f"  {GRN}All values found. Copy them onto your answer sheet.{OFF}")
    print()


if __name__ == "__main__":
    main()
