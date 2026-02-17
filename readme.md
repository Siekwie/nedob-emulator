# Nedob Emulator

A Nintendo 3DS emulator written in C++23 with an SDL3 frontend. It targets 3DS only (no NDS/DSi core).

## What’s implemented

- **3DS core**: ARM11 CPU interpreter, 3DS memory map, core timing.
- **Loader**: NCCH container parsing; CXI and CCI (.3ds) loading. **Decrypted** CXI/CCI only (encrypted dumps are not supported).
- **HLE**: Supervisor calls (SVC) and GSP (GPU service) stubs; basic GPU path for display.
- **Frontend**: SDL3 window and renderer; file picker to choose a ROM, then run emulation.

ROM type is detected by extension (`.3ds`, `.cci`, `.cia`, `.cxi`, `.app`, `.3dsx`). Only the 3DS core is used; other types are not supported.

## Build

**Requirements:** CMake 3.16+, C++23 compiler, C compiler.

**Vendored dependencies:**

- **SDL3** – built from source. Put the SDL source tree in `vendored/SDL` (this path is gitignored).
- **tiny_aes** – used for NCCH decryption; lives in `vendored/tiny_aes`.

**Unix (Linux/macOS):**

```bash
./build.sh
```

This does a clean configure and build. The executable is under `build/<Configuration>/NedobEMU`.

**Manual:**

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Inspiration

This project uses the [Azahar](https://github.com/azahar-emu/azahar) emulator as a reference for 3DS layout and services. A local copy can be kept at `insp/azahar` (gitignored).
