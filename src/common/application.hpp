#pragma once

#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include "../core/3ds/ThreeDsCore.hpp"

class Application {
public:
    Application();
    ~Application();

    bool initialize();
    bool loadROM(const std::string& path, const std::string& core_type);
    void run();

private:
    void processEvents(bool& running);
    void renderFrame();

    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};
    std::unique_ptr<ThreeDS> loaded_core_;
    bool paused_{false};
    uint64_t frame_count_{0};
};
