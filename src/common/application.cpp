#include "application.hpp"
#include "../cores/3ds/ThreeDsCore.hpp"
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>
#include <cstdio>

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
        std::fprintf(stderr, "Failed to create emulator window: %s\n", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        std::fprintf(stderr, "Failed to create renderer: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

bool Application::loadROM(const std::string& path, const std::string& core_type) {
    if (core_type != "3ds") {
        std::fprintf(stderr, "Only 3DS core is supported right now.\n");
        return false;
    }

    loaded_core_ = std::make_unique<ThreeDS>();
    return loaded_core_->loadROM(path);
}

void Application::run() {
    if (!loaded_core_) {
        std::fprintf(stderr, "No core loaded.\n");
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

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }

    const bool blink = (frame_count_ / 30) % 2 == 0;
    const uint8_t top_blue = blink ? 160 : 80;
    const uint8_t bottom_green = blink ? 160 : 80;

    SDL_SetRenderDrawColor(renderer_, 10, 10, 30, 255);
    SDL_RenderClear(renderer_);

    SDL_FRect top_rect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height / 2)};
    SDL_FRect bottom_rect{0.0f, static_cast<float>(height / 2), static_cast<float>(width),
                          static_cast<float>(height - (height / 2))};

    SDL_SetRenderDrawColor(renderer_, 30, 90, top_blue, 255);
    SDL_RenderFillRect(renderer_, &top_rect);

    SDL_SetRenderDrawColor(renderer_, 90, bottom_green, 30, 255);
    SDL_RenderFillRect(renderer_, &bottom_rect);

    SDL_RenderPresent(renderer_);

    if (frame_count_ % 60 == 0) {
        char title[64];
        std::snprintf(title, sizeof(title), "Nedob Emulator - Frame %llu",
                      static_cast<unsigned long long>(frame_count_));
        SDL_SetWindowTitle(window_, title);
    }

    ++frame_count_;
}
