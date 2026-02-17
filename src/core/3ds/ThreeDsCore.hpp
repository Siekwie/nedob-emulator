#pragma once

#include "../../common/common_types.hpp"
#include <memory>
#include <string>

class ArmInterpreter;
class MemorySystem;
class SvcDispatcher;
struct SDL_Renderer;
struct SDL_Window;
class GspHle;
namespace VideoCore { class Gpu; }

/**
 * 3DS emulator core. Runs one ARM11 application (game/ROM).
 * Memory map and behaviour are based on Azahar/Citra-style 3DS layout.
 */
class ThreeDS {
public:
    ThreeDS();
    ~ThreeDS();

    /// Load a ROM from path (.3ds, .cci, .cia, .cxi, .app). Returns true on success.
    /// Only decrypted CXI/CCI supported.
    bool loadROM(const std::string& path);

    /// Run one frame of emulation (CPU steps, then sync to display).
    void runFrame();

    void setPaused(bool paused) { paused_ = paused; }
    bool isPaused() const { return paused_; }

    /// Reset the emulated system (reload from current ROM path if any).
    void reset();

    /// Pass SDL window/renderer for video output.
    void setDisplay(SDL_Window* window, SDL_Renderer* renderer);

    /// Present framebuffer to the given renderer.
    void present(SDL_Renderer* renderer);

private:
    std::string rom_path_;
    bool rom_loaded_{false};
    bool paused_{false};
    uint64_t frame_count_{0};
    bool execution_stopped_{false};

    std::unique_ptr<MemorySystem> memory_;
    std::unique_ptr<ArmInterpreter> interpreter_;
    std::unique_ptr<SvcDispatcher> svc_dispatcher_;
    std::unique_ptr<class CoreTiming> timing_;
    std::unique_ptr<VideoCore::Gpu> gpu_;
    std::unique_ptr<class GspHle> gsp_hle_;
};
