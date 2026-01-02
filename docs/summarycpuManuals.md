### Summary of Provided CPU Technical Manuals

Here is a high-level summary of the architectural details from the manuals, focusing on what an emulator developer needs to know.

#### 1. ARM7TDMI (Secondary Processor in NDS & Security Processor in 3DS)

This is the simpler of the NDS processors, often handling I/O, sound, and system tasks.

- **Architecture:** ARMv4T
- **Instruction Sets:**
  - **ARM:** 32-bit, word-aligned instructions.
  - **Thumb:** 16-bit, halfword-aligned instruction set for better code density. The 'T' in ARM7TDMI signifies Thumb support.
- **Pipeline:** Simple 3-stage pipeline (Fetch, Decode, Execute). This is relatively straightforward to emulate.
- **Memory Model:** Von Neumann architecture (single bus for instructions and data).
- **Registers:**
  - 16 general-purpose 32-bit registers (R0-R15). R13 is the Stack Pointer (SP), R14 is the Link Register (LR), R15 is the Program Counter (PC).
  - Features banked registers for different operating modes (User, FIQ, IRQ, Supervisor, Abort, Undefined, System). You will need to implement this banking to handle interrupts and exceptions correctly.
- **Exceptions & Interrupts:**
  - Has two interrupt lines: standard Interrupt Request (IRQ) and Fast Interrupt Request (FIQ).
  - Exception vectors are at fixed low addresses (starting at `0x00000000`).
  - Handles Data Aborts, Prefetch Aborts, and Undefined Instruction traps.
- **Key Takeaway for Emulator Dev:** This is a classic, well-documented ARM core. Its primary emulation challenge is correctly handling the different operating modes, register banking, and the switch between ARM and Thumb states (`BX` instruction).

#### 2. ARM946E-S (Main Processor in NDS)

This is the main application processor on the NDS, running the game logic and setting up graphics.

- **Architecture:** ARMv5TExP
- **Instruction Sets:** ARMv4T set plus:
  - 'E' for Enhanced DSP instructions (for 16-bit and 8-bit data operations).
  - 'xP' for additional instructions.
- **Pipeline:** More complex 5-stage pipeline. For emulation, you can often simplify this, but instruction timing will be less accurate.
- **Memory Model:** Harvard architecture (separate paths for instructions and data), featuring:
  - **Caches:** Separate Instruction and Data Caches (ICache, DCache). Emulating caches is a significant step for performance and accuracy. Initially, you can treat all memory as uncached.
  - **Tightly-Coupled Memory (TCM):** A key feature. This is a small, fast SRAM mapped directly into the CPU's address space with zero wait states. The NDS relies heavily on TCMs for performance. Your memory management unit must correctly route accesses to I-TCM and D-TCM.
  - **Protection Unit (MPU):** This is _not_ a full MMU. It defines up to 8 memory regions with specific access permissions (read/write, privileged/user), cachability, and bufferability. It does _not_ perform virtual-to-physical address translation.
- **Key Takeaway for Emulator Dev:** The biggest step up from the ARM7 is the memory system. You must implement the MPU to handle memory permissions and attributes (cachable/bufferable). Emulating the TCMs is critical for getting games to run correctly and at a reasonable speed.

#### 3. ARM11 MPCore (Main Application Processor in 3DS)

This is a much more modern and complex CPU core that powers the 3DS. The "MP" stands for Multi-Processor.

- **Architecture:** ARMv6K
- **Core Features:**
  - **Multi-Processor:** The chip can contain up to 4 cores. The 3DS uses a dual-core ARM11 MPCore. Your emulator must be able to simulate multiple CPU cores running simultaneously.
  - **Vector Floating-Point (VFPv2):** Hardware floating-point acceleration, essential for 3D graphics calculations. This is a major unit to emulate.
  - **SIMD Instructions:** Enhanced media instructions for parallel operations on data.
- **Pipeline:** Deeper 8-stage pipeline.
- **Memory Model:** Full **Memory Management Unit (MMU)**. This is a critical difference from the ARM9's MPU.
  - Provides virtual-to-physical address translation.
  - Uses multi-level page tables to define memory attributes and permissions.
  - Your emulator needs a much more sophisticated memory management system to handle the 3DS's operating system, which uses virtual memory.
- **Interrupts:** Uses a **Distributed Interrupt Controller (DIC)** to manage and route interrupts from peripherals to the different CPU cores.
- **Key Takeaway for Emulator Dev:** This is a huge leap in complexity. You must emulate a multi-core system, a full MMU with page table walks, a VFP unit for floating point math, and the distributed interrupt controller.

### What These Manuals Don't Tell You (The Nintendo Hardware)

This is the most critical part. Emulating a console is ~20% emulating the CPU and ~80% emulating everything else. You will need to find documentation for these custom components.

#### For the Nintendo DS:

1.  **Memory Map:** The single most important piece of information. This tells you where everything is.
    - WRAM (Work RAM)
    - VRAM (Video RAM)
    - I/O Registers for all peripherals.
    - Cartridge ROM and save data (SAV).
2.  **Graphics Engines (PPU - Picture Processing Unit):** The NDS has two distinct 2D engines, one for each screen. They support:
    - Tiled backgrounds with affine transformations (scaling/rotation).
    - Hardware sprites (called OBJs).
    - A limited 3D rendering engine.
3.  **I/O Devices:**
    - **Touchscreen:** Reading coordinates.
    - **Buttons:** Reading input state.
    - **Cartridge Interface:** Reading game data from a ROM file.
    - **IPC (Inter-Processor Communication):** How the ARM9 and ARM7 communicate, primarily through shared RAM and FIFOs.
4.  **Sound Controller (DSP):** A dedicated sound processor that needs to be emulated to handle audio.
5.  **Boot Process:** The ARM7 boots first from an internal ROM (the BIOS), sets up the hardware, and then boots the ARM9 to run the game.

#### For the Nintendo 3DS:

This is an order of magnitude more complex.

1.  **System-on-Chip (SoC):** The ARM11 MPCore is just one part of the main chip.
2.  **GPU (Graphics Processing Unit):** The **PICA200** by DMP. This is a custom, programmable graphics processor with its own instruction set and vertex/fragment shaders. Emulating this is an enormous project in itself and completely separate from CPU emulation.
3.  **Multi-Core Architecture:** The 3DS has the dual-core ARM11 for applications, but also a separate **ARM9** processor (similar to the NDS one) that acts as a security and system coprocessor, handling things like the kernel and hardware access in the background.
4.  **Operating System:** The 3DS runs a microkernel-based OS. You need to understand its system calls, process management, memory layout, and services to emulate games, which are user-land applications.
5.  **I/O Devices:** 3D Stereoscopic Display, Circle Pad, Gyroscope, Accelerometer, Cameras.
6.  **Security:** An extremely complex security system involving the ARM9, encrypted bootroms, and hardware secrets.
