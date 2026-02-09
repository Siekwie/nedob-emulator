#pragma once

#include <cstdint>
#include <string>

/**
 * 3DS emulator core. Runs one ARM11 application (game/ROM).
 * Memory map and behaviour are based on Azahar/Citra-style 3DS layout.
 */
class ThreeDS {
public:
    ThreeDS();
    ~ThreeDS();

    /// Load a ROM from path (.3ds, .cci, .cia, .cxi, .app, .3dsx). Returns true on success.
    bool loadROM(const std::string& path);

    /// Run one frame of emulation (CPU steps, then sync to display).
    void runFrame();

    void setPaused(bool paused) { paused_ = paused; }
    bool isPaused() const { return paused_; }

    /// Reset the emulated system (reload from current ROM path if any).
    void reset();

private:
    std::string rom_path_;
    bool rom_loaded_{false};
    bool paused_{false};
    uint64_t frame_count_{0};
};
