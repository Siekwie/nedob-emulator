#!/usr/bin/env python3
"""
ARM "modified immediate" (imm12) decoder.

This encoding is used by A32 (ARM state) data-processing immediate instructions:
op2 = ROR(imm8, rot*2), where rot is a 4-bit field.
"""

from __future__ import annotations

import argparse


def _ror32(v: int, r: int) -> int:
    r &= 31
    v &= 0xFFFFFFFF
    return ((v >> r) | ((v << (32 - r)) & 0xFFFFFFFF)) & 0xFFFFFFFF


def expand_imm12(imm12: int) -> int:
    """Expand ARM imm12 (rot:imm8) to a 32-bit value."""
    imm12 &= 0xFFF
    imm8 = imm12 & 0xFF
    rot = ((imm12 >> 8) & 0xF) * 2
    return _ror32(imm8, rot)


def _parse_int(s: str) -> int:
    return int(s, 0)


def main() -> int:
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("imm12", nargs="?", help="imm12 value (e.g. 0x511)")
    g.add_argument("--inst", help="32-bit ARM instruction word (e.g. 0xE2400511)")
    ap.add_argument("--json", action="store_true", help="print machine-readable JSON-ish output")
    args = ap.parse_args()

    if args.inst is not None:
        inst = _parse_int(args.inst) & 0xFFFFFFFF
        imm12 = inst & 0xFFF
    else:
        imm12 = _parse_int(args.imm12) & 0xFFF
        inst = None

    imm32 = expand_imm12(imm12)
    rot = ((imm12 >> 8) & 0xF) * 2
    imm8 = imm12 & 0xFF

    if args.json:
        # Avoid non-stdlib json import; this is good enough for quick copy/paste.
        print("{")
        if inst is not None:
            print(f'  "inst": "0x{inst:08X}",')
        print(f'  "imm12": "0x{imm12:03X}",')
        print(f'  "rot": {rot},')
        print(f'  "imm8": {imm8},')
        print(f'  "imm32": "0x{imm32:08X}"')
        print("}")
        return 0

    if inst is not None:
        print(f"inst  = 0x{inst:08X}")
    print(f"imm12 = 0x{imm12:03X}  (rot={rot} imm8=0x{imm8:02X})")
    print(f"imm32 = 0x{imm32:08X}  ({imm32})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

