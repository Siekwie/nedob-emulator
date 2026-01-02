#include "frontend.hpp"
#include <SDL3/SDL_gpu.h>
#include <cstdio>

Frontend::Frontend()
    : window_(nullptr)
    , window_props_(0)
    , should_close_(false)
    , width_(800)
    , height_(600)
    , render_surface_(nullptr)
{
    resetInputState();
}

Frontend::~Frontend() {
    shutdown();
}

bool Frontend::initialize() {
    // Create window
    // For NDS, we'll use a window that can display dual screens
    // Start with a reasonable size - can be resized later
    window_ = SDL_CreateWindow(
        "Nedob Emulator",
        width_,
        height_,
        SDL_WINDOW_RESIZABLE
    );
    
    if (!window_) {
        std::fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        return false;
    }
    
    // Get window properties
    window_props_ = SDL_GetWindowProperties(window_);
    
    // Create software rendering surface (temporary)
    // NDS screens are 256x192 each, we'll display them side by side
    render_surface_ = SDL_CreateSurface(512, 192, SDL_PIXELFORMAT_XRGB8888);
    if (!render_surface_) {
        std::fprintf(stderr, "Failed to create render surface: %s\n", SDL_GetError());
        return false;
    }
    
    // Try to get window surface - SDL3 may need it to be created first
    SDL_Surface* test_surface = SDL_GetWindowSurface(window_);
    if (!test_surface) {
        std::fprintf(stderr, "Warning: Window surface not available initially: %s\n", SDL_GetError());
        // Surface might be created on first access, so this is not necessarily an error
    }
    
    // Initialize GPU (for future rendering)
    // For now, we'll skip GPU initialization until we have rendering code
    // if (!initializeGPU()) {
    //     std::fprintf(stderr, "Warning: GPU initialization failed\n");
    // }
    
    std::printf("Frontend initialized successfully\n");
    return true;
}

void Frontend::shutdown() {
    if (render_surface_) {
        SDL_DestroySurface(render_surface_);
        render_surface_ = nullptr;
    }
    
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

bool Frontend::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_QUIT:
            should_close_ = true;
            return true;
            
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == SDL_GetWindowID(window_)) {
                should_close_ = true;
                return true;
            }
            break;
            
        case SDL_EVENT_WINDOW_RESIZED:
            if (event.window.windowID == SDL_GetWindowID(window_)) {
                width_ = event.window.data1;
                height_ = event.window.data2;
                // TODO: Update viewport/render targets when GPU is initialized
                return true;
            }
            break;
            
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            updateInputState();
            return false;  // Let application handle some keys too
            
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_MOTION:
            updateInputState();
            return false;
            
        default:
            break;
    }
    
    return false;
}

void Frontend::present() {
    if (render_surface_ && window_) {
        // Blit the render surface to the window
        SDL_Surface* window_surface = SDL_GetWindowSurface(window_);
        if (window_surface) {
            // Check if formats match - if not, we might need to convert
            if (render_surface_->format != window_surface->format) {
                // Formats don't match - try direct blit without scaling first
                SDL_Rect src_rect = {0, 0, render_surface_->w, render_surface_->h};
                SDL_Rect dst_rect = {0, 0, render_surface_->w, render_surface_->h};
                if (!SDL_BlitSurface(render_surface_, &src_rect, window_surface, &dst_rect)) {
                    // Success
                } else {
                    static int error_count = 0;
                    if (++error_count <= 3) {
                        std::fprintf(stderr, "BlitSurface failed: %s\n", SDL_GetError());
                    }
                }
            } else {
                // Scale and blit (simple nearest neighbor for now)
                SDL_Rect src_rect = {0, 0, render_surface_->w, render_surface_->h};
                SDL_Rect dst_rect = {0, 0, width_, height_};
                if (SDL_BlitSurfaceScaled(render_surface_, &src_rect, window_surface, &dst_rect, SDL_SCALEMODE_NEAREST)) {
                    // Success - no error
                } else {
                    static int error_count = 0;
                    if (++error_count <= 3) {
                        std::fprintf(stderr, "BlitSurfaceScaled failed: %s\n", SDL_GetError());
                    }
                }
            }
            
            if (!SDL_UpdateWindowSurface(window_)) {
                static int error_count = 0;
                if (++error_count <= 3) {
                    std::fprintf(stderr, "UpdateWindowSurface failed: %s\n", SDL_GetError());
                }
            }
        } else {
            static int error_count = 0;
            if (++error_count == 1) {
                std::fprintf(stderr, "Window surface is null: %s\n", SDL_GetError());
            }
        }
    }
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
}

void Frontend::renderNDSScreens(const uint16_t* main_screen, const uint16_t* sub_screen) {
    if (!render_surface_ || !main_screen || !sub_screen) {
        static int null_count = 0;
        if (++null_count == 1) {
            std::fprintf(stderr, "renderNDSScreens called with null pointers\n");
        }
        return;
    }
    
    const int NDS_WIDTH = 256;
    const int NDS_HEIGHT = 192;
    
    // Lock surface for pixel access
    if (SDL_MUSTLOCK(render_surface_)) {
        SDL_LockSurface(render_surface_);
    }
    
    uint32_t* pixels = static_cast<uint32_t*>(render_surface_->pixels);
    int pitch = render_surface_->pitch / 4;  // Pitch in 32-bit pixels
    
    // Render main screen (left side)
    for (int y = 0; y < NDS_HEIGHT; y++) {
        for (int x = 0; x < NDS_WIDTH; x++) {
            uint16_t bgr555 = main_screen[y * NDS_WIDTH + x];
            uint32_t rgb888 = convertBGR555ToRGB888(bgr555);
            pixels[y * pitch + x] = rgb888;
        }
    }
    
    // Render sub screen (right side)
    for (int y = 0; y < NDS_HEIGHT; y++) {
        for (int x = 0; x < NDS_WIDTH; x++) {
            uint16_t bgr555 = sub_screen[y * NDS_WIDTH + x];
            uint32_t rgb888 = convertBGR555ToRGB888(bgr555);
            pixels[y * pitch + (x + NDS_WIDTH)] = rgb888;
        }
    }
    
    if (SDL_MUSTLOCK(render_surface_)) {
        SDL_UnlockSurface(render_surface_);
    }
}

uint32_t Frontend::convertBGR555ToRGB888(uint16_t bgr555) const {
    uint8_t r = ((bgr555 >> 0) & 0x1F) << 3;
    uint8_t g = ((bgr555 >> 5) & 0x1F) << 3;
    uint8_t b = ((bgr555 >> 10) & 0x1F) << 3;
    return (r << 16) | (g << 8) | b;
}

void Frontend::setTitle(const std::string& title) {
    if (window_) {
        SDL_SetWindowTitle(window_, title.c_str());
    }
}

bool Frontend::initializeGPU() {
    // TODO: Initialize SDL3 GPU device
    // This will be implemented when we add rendering
    return false;
}

void Frontend::updateInputState() {
    // Reset state first
    resetInputState();
    
    // Get keyboard state
    int num_keys = 0;
    const bool* keyboard_state = SDL_GetKeyboardState(&num_keys);
    
    // Map keyboard keys to NDS buttons
    // Default mapping (can be made configurable later):
    // A = X, B = Z, X = S, Y = A
    // L = Q, R = W
    // Start = Enter, Select = Shift
    // D-Pad = Arrow keys
    
    input_state_.a = keyboard_state[SDL_SCANCODE_X];
    input_state_.b = keyboard_state[SDL_SCANCODE_Z];
    input_state_.x = keyboard_state[SDL_SCANCODE_S];
    input_state_.y = keyboard_state[SDL_SCANCODE_A];
    
    input_state_.l = keyboard_state[SDL_SCANCODE_Q];
    input_state_.r = keyboard_state[SDL_SCANCODE_W];
    
    input_state_.start = keyboard_state[SDL_SCANCODE_RETURN];
    input_state_.select = keyboard_state[SDL_SCANCODE_LSHIFT];
    
    input_state_.up = keyboard_state[SDL_SCANCODE_UP];
    input_state_.down = keyboard_state[SDL_SCANCODE_DOWN];
    input_state_.left = keyboard_state[SDL_SCANCODE_LEFT];
    input_state_.right = keyboard_state[SDL_SCANCODE_RIGHT];
    
    // TODO: Handle mouse/touch input for touchscreen emulation
    // For now, touch is not implemented
    input_state_.touch_pressed = false;
    input_state_.touch_x = 0;
    input_state_.touch_y = 0;
}

void Frontend::resetInputState() {
    input_state_ = {};
}
