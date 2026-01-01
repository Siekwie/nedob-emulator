# Nedob Emulator

### 1. Core Architecture: The "Unified" Bridge

To handle three systems in one executable, you need a modular core.

- **Abstraction Layer:** Create a `System` interface. Concrete classes `SystemDS` and `System3DS` will implement specific memory maps and everything else.
- **Common Components:** Use a shared `ARM` core for JIT ( eg. a custom interpreter ) since all three systems rely on ARM architectures (ARM7TDMI, ARM946E-S, and ARM11).
- **Memory Bus:** Implement a switchable memory bus. 3DS mode will require a much larger address space and different MMU configurations compared to DS mode.

### 2. CPU Implementation

- **DS/DS Lite:** You need dual-core synchronization between the ARM9 (logic/video) and ARM7 (sound/Wi-Fi/sub-processors).
- **3DS:** Focus on the ARM11 MPCore. For HLE, you will intercept Supervisor Calls (SVCs) to simulate the Horizon OS kernel.
- **HLE Strategy:** Instead of emulating the BIOS/Firmware ROMs exactly, write C++ functions that mimic the results of system calls (e.g., file I/O, threading, and resource management).

### 3. Graphics Pipeline (SDL3 GPU)

The SDL3 GPU API is ideal for this because it abstracts Vulkan, Metal, and DirectX 12.

- **DS Rendering:** The DS uses a 2D engine (PPU) and a fixed-function 3D engine. You can map the DS geometry engine to a vertex buffer and use custom shaders in SDL3 to replicate the cell-shading and texture mapping.
- **3DS Rendering (PICA200):** This is more complex, involving "procedural textures" and specific lighting models. You will need to write GLSL/HLSL shaders that SDL3 can compile into SPIR-V/MSL to mimic the PICA200's fragment pipeline.
- **Dual Screen Setup:** Use `SDL_GPURenderPass` to manage two separate viewports on a single window, or utilize SDL3's improved multi-window support for a "detached screen" mode.

### 4. HLE Service Simulation

This is where the "High Level" happens. You must implement the system services:

- **FS (File System):** Map the virtual SD card and NAND to local folders on the host machine.
- **HID (Human Interface Device):** Map SDL3 controller/touch events to the emulated touchscreen coordinates and button registers.
- **DSP (Digital Signal Processor):** For 3DS audio, you'll likely need to HLE the DSP binary to output PCM data directly to `SDL_AudioStream`.

### 5. Development Roadmap

| Phase   | Task              | Focus                                                                                 |
| :------ | :---------------- | :------------------------------------------------------------------------------------ |
| **I**   | **Foundation**    | Set up SDL3 window and GPU device initialization. Implement the base ARM interpreter. |
| **II**  | **DS Mode**       | Implement the ARM9/ARM7 bus. Get simple homebrew running.                             |
| **III** | **SDL3 Video**    | Create the 2D/3D rasterizer using `SDL_GPU`. Focus on DS layers first.                |
| **IV**  | **3DS Expansion** | Add ARM11 support and the VRAM/PICA200 command buffer parser.                         |
| **V**   | **HLE Services**  | Implement OS calls for loading commercial encrypted CIAs or .NDS files.               |

### Technical Considerations

- **Synchronization:** How do you plan to handle the timing difference between the ARM9 (67MHz) and the ARM11 (268MHz+)? Using a cycle-accurate scheduler is usually best for DS, but for 3DS HLE, a multi-threaded approach might be necessary for performance.
- **SDL3 GPU:** Have you experimented with the new `SDL_CreateGPUGraphicsPipeline`? It requires a strict state definition which is great for emulating fixed-function hardware.

**Curious thought:** Since you are targeting DS Lite as well, will you implement the SLOT-2 (GBA) functionality? It shares the same ARM7 bus and could be an interesting "feature creep" for later!
