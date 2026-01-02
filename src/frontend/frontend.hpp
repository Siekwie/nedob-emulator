#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <memory>
#include <string>

/**
 * Frontend class handles all SDL3 interactions:
 * - Window management
 * - SDL3 GPU initialization and rendering
 * - Input handling
 * - Audio output
 * - UI rendering
 */
class Frontend {
public:
    Frontend();
    ~Frontend();
    
    /**
     * Initialize the frontend (create window, GPU device, etc.).
     * 
     * @return true on success, false on failure
     */
    bool initialize();
    
    /**
     * Shutdown and cleanup.
     */
    void shutdown();
    
    /**
     * Handle an SDL event.
     * 
     * @param event The SDL event
     * @return true if the event was handled, false otherwise
     */
    bool handleEvent(const SDL_Event& event);
    
    /**
     * Present the current frame to the screen.
     * This should be called once per frame after the core has rendered.
     */
    void present();
    
    /**
     * Render NDS screens (main and sub).
     * 
     * @param main_screen Main screen buffer (256x192, BGR555 format)
     * @param sub_screen Sub screen buffer (256x192, BGR555 format)
     */
    void renderNDSScreens(const uint16_t* main_screen, const uint16_t* sub_screen);
    
    /**
     * Get the SDL window.
     */
    SDL_Window* getWindow() const { return window_; }
    
    /**
     * Check if the window should close.
     */
    bool shouldClose() const { return should_close_; }
    
    /**
     * Get the window width.
     */
    int getWidth() const { return width_; }
    
    /**
     * Get the window height.
     */
    int getHeight() const { return height_; }
    
    /**
     * Set the window title.
     */
    void setTitle(const std::string& title);
    
    /**
     * Get input state (for passing to core).
     * This is a placeholder - will be expanded with actual input handling.
     */
    struct InputState {
        bool a, b, x, y;
        bool l, r;
        bool start, select;
        bool up, down, left, right;
        int touch_x, touch_y;
        bool touch_pressed;
    };
    
    InputState getInputState() const { return input_state_; }

private:
    SDL_Window* window_;
    SDL_PropertiesID window_props_;
    bool should_close_;
    int width_;
    int height_;
    
    InputState input_state_;
    
    // Software rendering surface (temporary until GPU is set up)
    SDL_Surface* render_surface_;
    
    /**
     * Initialize SDL3 GPU device.
     * TODO: Implement when we add GPU rendering
     */
    bool initializeGPU();
    
    /**
     * Convert BGR555 pixel to RGB888.
     */
    uint32_t convertBGR555ToRGB888(uint16_t bgr555) const;
    
    /**
     * Update input state from SDL events.
     */
    void updateInputState();
    
    /**
     * Reset input state.
     */
    void resetInputState();
};
