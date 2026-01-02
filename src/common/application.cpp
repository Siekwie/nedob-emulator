#include "application.hpp"
#include "../frontend/frontend.hpp"
#include "../cores/nds/nds_core.hpp"
#include "../cores/nds/ppu.hpp"
#include "emulator_core.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <cstdio>
#include <algorithm>

Application::Application()
    : running_(false)
    , paused_(false)
    , target_frame_rate_(60.0)  // NDS runs at ~60 FPS
    , frame_rate_(0.0)
    , frame_time_accumulator_(0.0)
{
}

Application::~Application() {
    // Cleanup is handled by unique_ptr destructors
}

bool Application::initialize() {
    // Frontend will be initialized when we load a ROM
    // For now, just mark as ready
    running_ = true;
    last_frame_time_ = std::chrono::high_resolution_clock::now();
    return true;
}

bool Application::loadROM(const std::string& rom_path, const std::string& core_type) {
    // Create frontend first (needed for core initialization)
    frontend_ = std::make_unique<Frontend>();
    if (!frontend_->initialize()) {
        std::fprintf(stderr, "Failed to initialize frontend\n");
        return false;
    }
    
    // Create appropriate core based on type
    if (core_type == "nds") {
        core_ = std::make_unique<NDSCore>();
    } else if (core_type == "3ds") {
        // TODO: Implement 3DS core
        std::fprintf(stderr, "3DS core not yet implemented\n");
        return false;
    } else {
        std::fprintf(stderr, "Unknown core type: %s\n", core_type.c_str());
        return false;
    }
    
    // Load ROM into core
    if (!core_->loadROM(rom_path)) {
        std::fprintf(stderr, "Failed to load ROM: %s\n", rom_path.c_str());
        return false;
    }
    
    // Connect core to frontend
    core_->setFrontend(frontend_.get());
    
    std::printf("ROM loaded successfully: %s\n", rom_path.c_str());
    std::printf("Core type: %s\n", core_type.c_str());
    
    return true;
}

void Application::run() {
    if (!running_ || !core_ || !frontend_) {
        std::fprintf(stderr, "Application not properly initialized\n");
        return;
    }
    
    const double target_frame_time = 1.0 / target_frame_rate_;
    
    while (running_) {
        frame_start_time_ = std::chrono::high_resolution_clock::now();
        
        // Handle events (input, window close, etc.)
        handleEvents();
        
        if (!paused_) {
            // Run emulation for one frame
            processFrame();
        }
        
        // Get frame from core and present to frontend
        if (core_ && frontend_) {
            // Get PPU frame buffers and render them
            NDSCore* nds_core = dynamic_cast<NDSCore*>(core_.get());
            if (nds_core && nds_core->getPPU()) {
                PPU* ppu = nds_core->getPPU();
                frontend_->renderNDSScreens(ppu->getMainScreenBuffer(), ppu->getSubScreenBuffer());
            }
        }
        
        // Present frame to frontend
        frontend_->present();
        
        // Frame rate limiting
        auto frame_end_time = std::chrono::high_resolution_clock::now();
        auto frame_duration = std::chrono::duration<double>(frame_end_time - frame_start_time_).count();
        
        if (frame_duration < target_frame_time) {
            // Sleep to maintain target frame rate
            auto sleep_time = target_frame_time - frame_duration;
            SDL_Delay(static_cast<Uint32>(sleep_time * 1000.0));
        }
        
        // Update frame rate statistics
        updateFrameRate();
        last_frame_time_ = frame_start_time_;
    }
}

void Application::requestExit() {
    running_ = false;
}

void Application::reset() {
    if (core_) {
        core_->reset();
    }
}

bool Application::saveState(const std::string& filepath) {
    if (core_) {
        return core_->saveState(filepath);
    }
    return false;
}

bool Application::loadState(const std::string& filepath) {
    if (core_) {
        return core_->loadState(filepath);
    }
    return false;
}

void Application::processFrame() {
    if (core_) {
        // Run core for one frame
        // The core will handle its own timing internally
        core_->runFrame();
    }
}

void Application::updateFrameRate() {
    auto current_time = std::chrono::high_resolution_clock::now();
    auto delta_time = std::chrono::duration<double>(current_time - last_frame_time_).count();
    
    if (delta_time > 0.0) {
        // Exponential moving average for smooth frame rate display
        double instant_fps = 1.0 / delta_time;
        frame_rate_ = frame_rate_ * 0.9 + instant_fps * 0.1;
    }
}

void Application::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Let frontend handle events first (input, window management)
        if (frontend_) {
            if (frontend_->handleEvent(event)) {
                continue;  // Event was handled by frontend
            }
        }
        
        // Handle application-level events
        switch (event.type) {
            case SDL_EVENT_QUIT:
                requestExit();
                break;
                
            case SDL_EVENT_KEY_DOWN:
                // Handle global hotkeys
                if (event.key.key == SDLK_ESCAPE) {
                    requestExit();
                } else if (event.key.key == SDLK_P) {
                    setPaused(!paused_);
                    std::printf("Emulation %s\n", paused_ ? "paused" : "resumed");
                } else if (event.key.key == SDLK_R && (event.key.mod & SDL_KMOD_CTRL)) {
                    reset();
                    std::printf("Core reset\n");
                }
                break;
                
            default:
                break;
        }
        
        // Pass events to core for input handling
        if (core_ && !paused_) {
            core_->handleEvent(event);
        }
    }
}
