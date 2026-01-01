## 0) Reality check

- **DS and DS-Lite are essentially the same target** (same DS hardware). Treat them as one “NDS” core with a **device profile** (backlight levels, default settings, minor quirks).
- **3DS is a different target**: different CPUs, OS/services, GPU, security/encryption, filesystem/NAND. Keep **one emulator app**, but internally end up with **two cores** (NDS + 3DS) sharing a lot of infrastructure.

## 1) Top-level architecture

### A. `frontend/` (SDL3)

- Windowing, input, audio device, file dialogs, controller mapping
- UI (in-app menu, settings, per-game overrides)
- Uses **SDL3 GPU** for presenting frames, scaling, shaders (scanlines, LCD grid, color correction), and compositing dual screens

### B. `common/` (shared emulation framework)

- Scheduler / event loop (cycle/tick-based)
- Memory abstractions (MMU helpers, page tables, read/write tracing hooks)
- Save states framework (versioned serialization)
- Logging, tracing, profilers, determinism tools
- Config system, compatibility database, shader/pipeline cache
- JIT infrastructure (optional later): IR + backend(s)

### C. `cores/nds/` (DS + DS-Lite)

- ARM9 + ARM7 CPU cores, NDS memory map, DMA, timers, interrupts
- PPU (2D engines A/B), 3D engine, VRAM mapping
- SPU audio, input, touchscreen, cartridge + save types
- Minimal “HLE BIOS/SWI” layer (or LLE-ish implementations of SWIs where needed)

### D. `cores/3ds/` (3DS)

- ARM11 MPCore + ARM9 (at least enough to satisfy titles you target)
- Kernel/SVC + **IPC + services HLE** (FS, HID, GSP/GPU, DSP, etc.)
- PICA200 GPU command processor → translate to SDL3 GPU pipelines
- Title loading (CIA/NCCH), save data, extdata
- Crypto plumbing (user-supplied keys only)

### E. `file_selector/`

- A file selection, "type-detection" layer:
  - `.nds` → NDS core
  - `.3ds` etc. → 3ds core
- Small selection UI/UX before the actual emulation application starts.

---

## 2) SDL3 GPU usage strategy

Treat SDL3 GPU as a **presentation + hardware acceleration layer**.

- Build a `RenderBackend` interface:
  - `CreateTexture(width,height,format)`
  - `UploadFrame(...)`
  - `DrawComposite(screens, layout, filters)`
- For NDS:
  - Easiest: render in software to RGBA, upload 2 textures, composite in SDL3 GPU
  - Later: accelerate some PPU paths, but keep a software reference renderer for correctness
- For 3DS:
  - will likely do a **GPU HLE translation layer**:
    - Parse PICA command buffers → map to modern pipeline states + shader variants
    - Cache pipeline objects (SDL3 GPU pipelines)
  - Keep a “debug/validation” path (even if slow) to catch GPU state bugs early

---
