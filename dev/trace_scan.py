#!/usr/bin/env python3
"""
Trace-log helpers for Nedob bringup.

Expected inputs:
- The trace files produced when running with NEDOB_TRACE=1 (and optionally
  NEDOB_LOG_REG_WRITES=1).
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import deque
from typing import Deque, Iterable, Optional, Tuple


_RE_PC_FIELD = re.compile(r"\bPC=0x([0-9A-Fa-f]{8})\b")
_RE_REG_WRITE = re.compile(r"^\s*REG W (R(?:1[0-5]|[0-9]|1[0-5]))\s*=\s*(0x[0-9A-Fa-f]{8})")


def _iter_lines(path: str) -> Iterable[str]:
    with open(path, "r", errors="replace") as f:
        for line in f:
            yield line.rstrip("\n")


def _parse_int(s: str) -> int:
    return int(s, 0)


def _find_first_pc_line(path: str) -> Optional[Tuple[int, int, str]]:
    for ln, line in enumerate(_iter_lines(path), 1):
        m = _RE_PC_FIELD.search(line)
        if not m:
            continue
        return ln, int(m.group(1), 16), line
    return None


def cmd_first_wild_pc(args: argparse.Namespace) -> int:
    lo = _parse_int(args.low_ok)
    hi = _parse_int(args.high_ok)

    for ln, line in enumerate(_iter_lines(args.path), 1):
        m = _RE_PC_FIELD.search(line)
        if not m:
            continue
        pc = int(m.group(1), 16)
        if pc < lo or pc >= hi:
            print(f"{args.path}:{ln}: wild PC=0x{pc:08X}")
            print(line)
            return 0

    print("no wild PC found")
    return 1


def cmd_around_pc(args: argparse.Namespace) -> int:
    want = _parse_int(args.pc) & 0xFFFFFFFF
    ctx = int(args.context)

    # Print a small window around the first match.
    before: Deque[Tuple[int, str]] = deque(maxlen=ctx)
    after_remaining = 0

    for ln, line in enumerate(_iter_lines(args.path), 1):
        if after_remaining > 0:
            print(f"{ln}:{line}")
            after_remaining -= 1
            continue

        if f"0x{want:08X}" in line:
            for bln, bline in before:
                print(f"{bln}:{bline}")
            print(f"{ln}:{line}")
            after_remaining = ctx
            continue

        before.append((ln, line))

    return 0


def cmd_reg_writes(args: argparse.Namespace) -> int:
    reg = args.reg.upper()
    matched = 0
    for ln, line in enumerate(_iter_lines(args.path), 1):
        m = _RE_REG_WRITE.match(line)
        if not m:
            continue
        if m.group(1).upper() != reg:
            continue
        matched += 1
        print(f"{args.path}:{ln}: {line}")

    return 0 if matched else 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    ap_wild = sub.add_parser("first-wild-pc", help="print first PC outside the allowed range")
    ap_wild.add_argument("path", help="trace log path")
    ap_wild.add_argument("--low-ok", default="0x00100000", help="low inclusive bound (default: 0x00100000)")
    ap_wild.add_argument("--high-ok", default="0x10000000", help="high exclusive bound (default: 0x10000000)")
    ap_wild.set_defaults(func=cmd_first_wild_pc)

    ap_around = sub.add_parser("around-pc", help="print context around first line containing the PC")
    ap_around.add_argument("path", help="trace log path")
    ap_around.add_argument("pc", help="PC value (e.g. 0x00104858)")
    ap_around.add_argument("-C", "--context", default=20, help="lines of context before/after")
    ap_around.set_defaults(func=cmd_around_pc)

    ap_regs = sub.add_parser("reg-writes", help="list register writes from NEDOB_LOG_REG_WRITES output")
    ap_regs.add_argument("path", help="trace log path")
    ap_regs.add_argument("reg", help="register name, e.g. R5")
    ap_regs.set_defaults(func=cmd_reg_writes)

    args = ap.parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

