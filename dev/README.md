# Dev Utilities

Small helper scripts used while bringing up the emulator. These are intentionally
dependency-free (Python stdlib only) so they work on a fresh system.

## Scripts

### `arm_imm12.py`

Decode ARM "modified immediate" (imm12) values (the immediate encoding used by
ARM data-processing immediate instructions).

Examples:

```bash
python3 dev/arm_imm12.py 0x511
python3 dev/arm_imm12.py --inst 0xE2400511
```

### `arm_inst.py`

Quick ARM (A32) instruction field dump for common cases we hit during bringup
(condition, opcode, registers, imm12 decoding).

Examples:

```bash
python3 dev/arm_inst.py 0xE25009BA
python3 dev/arm_inst.py 0x5A000005
```

### `trace_scan.py`

Scan Nedob trace logs (the ones produced by `NEDOB_TRACE=1`) for common patterns.

Examples:

```bash
# Find first "wild PC" (left the usual code region)
python3 dev/trace_scan.py first-wild-pc /path/to/trace.txt

# Print a small window around the first occurrence of a PC
python3 dev/trace_scan.py around-pc /path/to/trace.txt 0x00104858 -C 20

# List register writes (as emitted by NEDOB_LOG_REG_WRITES=1)
python3 dev/trace_scan.py reg-writes /path/to/trace.txt R5
```

