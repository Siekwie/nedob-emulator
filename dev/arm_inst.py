#!/usr/bin/env python3
"""
Quick A32 (ARM state) instruction field dump for bringup/debugging.

Not a full disassembler; this is meant to sanity-check immediates/fields when
comparing traces against expectations.
"""

from __future__ import annotations

import argparse


def _ror32(v: int, r: int) -> int:
    r &= 31
    v &= 0xFFFFFFFF
    return ((v >> r) | ((v << (32 - r)) & 0xFFFFFFFF)) & 0xFFFFFFFF


def expand_imm12(imm12: int) -> int:
    imm12 &= 0xFFF
    imm8 = imm12 & 0xFF
    rot = ((imm12 >> 8) & 0xF) * 2
    return _ror32(imm8, rot)


COND = {
    0x0: "EQ",
    0x1: "NE",
    0x2: "CS/HS",
    0x3: "CC/LO",
    0x4: "MI",
    0x5: "PL",
    0x6: "VS",
    0x7: "VC",
    0x8: "HI",
    0x9: "LS",
    0xA: "GE",
    0xB: "LT",
    0xC: "GT",
    0xD: "LE",
    0xE: "AL",
    0xF: "NV",
}

OPC = {
    0x0: "AND",
    0x1: "EOR",
    0x2: "SUB",
    0x3: "RSB",
    0x4: "ADD",
    0x5: "ADC",
    0x6: "SBC",
    0x7: "RSC",
    0x8: "TST",
    0x9: "TEQ",
    0xA: "CMP",
    0xB: "CMN",
    0xC: "ORR",
    0xD: "MOV",
    0xE: "BIC",
    0xF: "MVN",
}


def _parse_int(s: str) -> int:
    return int(s, 0)


def dump(inst: int) -> None:
    inst &= 0xFFFFFFFF
    cond = (inst >> 28) & 0xF
    bits27_25 = (inst >> 25) & 0x7
    bits27_26 = (inst >> 26) & 0x3

    print(f"inst       0x{inst:08X}")
    print(f"cond       0x{cond:X} ({COND.get(cond, '??')})")
    print(f"bits27_26  {bits27_26:b}  bits27_25 {bits27_25:b}")

    # Branch (B/BL)
    if bits27_25 == 0b101:
        L = (inst >> 24) & 1
        imm24 = inst & 0xFFFFFF
        # Sign-extend then << 2
        if imm24 & 0x800000:
            imm24 |= ~0xFFFFFF
        off = (imm24 << 2) & 0xFFFFFFFF
        print(f"type       branch {'BL' if L else 'B'}")
        print(f"L          {L}")
        print(f"imm24      0x{(inst & 0xFFFFFF):06X}  off(bytes)=0x{off:08X} ({off if off < (1<<31) else off-(1<<32)})")
        return

    # SVC (formerly SWI): bits[27:24] == 0b1111, imm24 in low bits.
    if (inst & 0x0F000000) == 0x0F000000:
        # Kept here mostly as a reminder; we usually spot SVC easily.
        imm24 = inst & 0x00FFFFFF
        print("type       SVC")
        print(f"imm24      0x{imm24:06X}")
        return

    # Data-processing
    if bits27_26 == 0b00:
        I = (inst >> 25) & 1
        op = (inst >> 21) & 0xF
        S = (inst >> 20) & 1
        rn = (inst >> 16) & 0xF
        rd = (inst >> 12) & 0xF
        print("type       data-processing")
        print(f"I          {I}  S={S}")
        print(f"op         0x{op:X} ({OPC.get(op, '??')})")
        print(f"Rn         R{rn}  Rd R{rd}")
        if I:
            imm12 = inst & 0xFFF
            rot = ((imm12 >> 8) & 0xF) * 2
            imm8 = imm12 & 0xFF
            imm32 = expand_imm12(imm12)
            print(f"imm12      0x{imm12:03X} (rot={rot} imm8=0x{imm8:02X}) -> 0x{imm32:08X}")
        else:
            rm = inst & 0xF
            shift_type = (inst >> 5) & 0x3
            shift_imm = (inst >> 7) & 0x1F
            print(f"Rm         R{rm}  shift_type={shift_type} shift_imm={shift_imm}")
        return

    print("type       (unhandled; use emulator disasm/log for details)")


def bits27_24(inst: int) -> int:
    return (inst >> 24) & 0xF


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("inst", help="32-bit ARM instruction word (e.g. 0xE25009BA)")
    args = ap.parse_args()
    dump(_parse_int(args.inst))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

