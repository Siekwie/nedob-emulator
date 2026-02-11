#include "application.hpp"
#include "../core/3ds/ThreeDsCore.hpp"
#include "logger.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

Application::Application() = default;

Application::~Application() {
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

bool Application::initialize() {
    window_ = SDL_CreateWindow("Nedob Emulator", 512, 384, 0);
    if (!window_) {
        Logger::log("Failed to create emulator window: %s\n", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        Logger::log("Failed to create renderer: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

bool Application::loadROM(const std::string& path, const std::string& core_type) {
    if (core_type != "3ds") {
        Logger::log("Only 3DS core is supported right now.\n");
        return false;
    }

    loaded_core_ = std::make_unique<ThreeDS>();
    if (!loaded_core_->loadROM(path)) {
        return false;
    }
    loaded_core_->setDisplay(window_, renderer_);
    return true;
}

void Application::run() {
    if (!loaded_core_) {
        Logger::log("No core loaded.\n");
        return;
    }

    bool running = true;
    while (running) {
        processEvents(running);

        if (!paused_) {
            loaded_core_->runFrame();
        }

        renderFrame();
        SDL_Delay(16);
    }
}

void Application::processEvents(bool& running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        } else if (event.type == SDL_EVENT_KEY_DOWN) {
            const SDL_Keycode key = event.key.key;
            if (key == SDLK_ESCAPE) {
                running = false;
            } else if (key == SDLK_P) {
                paused_ = !paused_;
                if (loaded_core_) {
                    loaded_core_->setPaused(paused_);
                }
            } else if (key == SDLK_R &&
                       (event.key.mod & SDL_KMOD_CTRL)) {
                if (loaded_core_) {
                    loaded_core_->reset();
                }
            }
        }
    }
}

void Application::renderFrame() {
    if (!renderer_) {
        return;
    }

    if (loaded_core_) {
        loaded_core_->present(renderer_);
    } else {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        if (width > 0 && height > 0) {
            SDL_SetRenderDrawColor(renderer_, 20, 20, 30, 255);
            SDL_RenderClear(renderer_);
        }
    }

    SDL_RenderPresent(renderer_);

    if (frame_count_ % 60 == 0 && loaded_core_) {
        char title[64];
        std::snprintf(title, sizeof(title), "Nedob Emulator - Frame %llu",
                      static_cast<unsigned long long>(frame_count_));
        SDL_SetWindowTitle(window_, title);
    }

    ++frame_count_;
}
