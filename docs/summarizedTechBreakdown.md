### 1. Processor Architectures

The NDS utilizes a dual-core setup which corresponds to these manuals:

- **ARM946E-S (Main CPU):** Handles 3D graphics and complex logic. It uses a Harvard architecture (separate instruction and data buses).
- **ARM7TDMI (Sub CPU):** Handles 2D graphics, sound, and Wi-Fi. It uses a Von Neumann architecture (unified bus).

---

### 2. Memory Map and Addressing

#### ARM946E-S Specifics

- **Tightly-Coupled Memory (TCM):**
  - **I-SRAM:** Instruction SRAM located at `0x00000000`. Used for high-speed instruction fetching without stall cycles (Page 5-2).
  - **D-SRAM:** Data SRAM for high-speed data access. Size is implementor-configurable (Page 2-10).
- **Caches:** Supports four-way set-associative caches ranging from 4KB to 1MB. Emulation must account for "allocate on read-miss" policies (Page 3-2).
- **Protection Unit (MPU):** Allows partitioning memory into 8 programmable regions. Region 7 has the highest priority; Region 0 is the lowest (Page 4-6).

#### ARM7TDMI Specifics

- **Data Types:** Supports 32-bit (words), 16-bit (halfwords), and 8-bit (bytes). Words must be 4-byte aligned (Page 2-6).
- **Endianness:** Both processors support Little-endian and Big-endian modes, though NDS typically operates in Little-endian (Page 2-4).

---

### 3. Interrupt Handling

Both cores use a prioritized exception system. On an interrupt, the processor saves the current state (CPSR) to the SPSR of the new mode and jumps to a specific vector address.

| Exception              | Vector Address         | Mode on Entry | Priority    |
| :--------------------- | :--------------------- | :------------ | :---------- |
| Reset                  | `0x00000000`           | Supervisor    | 1 (Highest) |
| Data Abort             | `0x00000010`           | Abort         | 2           |
| FIQ (Fast Interrupt)   | `0x0000001C`           | FIQ           | 3           |
| IRQ (Normal Interrupt) | `0x00000018`           | IRQ           | 4           |
| Prefetch Abort         | `0x0000000C`           | Abort         | 5           |
| SWI / Undefined        | `0x00000008` / `...04` | SVC / UND     | 6 (Lowest)  |

- **Interrupt Latency:** On the ARM7TDMI, the worst-case FIQ latency is 29 cycles, and minimum is 5 cycles (Page 2-23).
- **Return Instructions:**
  - **IRQ/FIQ:** `SUBS PC, R14_irq, #4`
  - **Data Abort:** `SUBS PC, R14_abt, #8`

---

### 4. Bus Structure and I/O

#### AHB (Advanced High-performance Bus)

The ARM946E-S uses the AMBA AHB interface for external memory access.

- **Transfer Types:** IDLE (00), NONSEQ (10), and SEQ (11). Emulators must handle sequential bursts correctly to optimize timing (Page 6-3).
- **Wait States:** The `nWAIT` (ARM7) and `HREADY` (ARM9) signals allow slow peripherals to stretch memory cycles.

#### Coprocessor Interface (CP15)

The ARM9 uses CP15 for system control. Key registers to emulate:

- **Register 1 (Control):** Enables Caches, MPU, and TCM (Page 2-11).
- **Register 9:** Controls Cache Lockdown and TCM region sizes (Page 2-25).
- **Register 13:** Process ID for trace/debug (Page 2-28).

---

### 5. Instruction Set and Pipeline

#### Operating States

1.  **ARM State:** 32-bit, word-aligned instructions.
2.  **Thumb State:** 16-bit, halfword-aligned instructions. Provides ~65% code density of ARM (Page 1-6).

- **Pipeline:** Both processors use a 3-stage pipeline (Fetch-Decode-Execute). The PC value used in an instruction is always **8 bytes ahead** (2 instructions) of the current instruction address in ARM state (Page 1-3).

#### Special Operations

- **Data Swap (SWP):** Atomically exchanges a register value with a memory location. This is crucial for synchronizing the two CPUs (Page 6-18).
- **Wait for Interrupt:** The ARM946E-S can enter a low-power standby mode via an MCR instruction to Register 7, stalling the core until an interrupt occurs (Page 2-24).

---

### 6. Debug and Trace

- **EmbeddedICE:** Integrated logic providing two watchpoint units for breakpoints and data watchpoints.
- **DCC (Debug Communications Channel):** A dedicated link (Coprocessor 14) for passing information between the target and the host (Page 8-30).
- **ETM (Embedded Trace Macrocell):** Used on the ARM9 to provide real-time tracing of instructions and data via a pipelined one-way interface (Page 9-2).

### 7. Critical Emulation Checklist

1.  **Banked Registers:** Implement separate R13/R14 for SVC, IRQ, FIQ, Abort, and Undefined modes. FIQ has additional banked registers (R8-R12).
2.  **Pipeline Flush:** On every branch or exception, the 3-stage pipeline must be flushed and refilled.
3.  **Write Buffer:** The ARM9 has a 16-entry FIFO write buffer. Emulate this to ensure correct memory synchronization between the cache and external RAM (Page 6-12).
4.  **TCM Priority:** On the ARM9, TCM always takes precedence over Cache and AHB for addresses within its range (Page 5-4).
