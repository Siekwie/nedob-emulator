#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <functional>

// Forward declarations
class EmulatorCore;
class Frontend;

/**
 * Main application class that manages the emulator lifecycle.
 * 
 * Responsibilities:
 * - Time management and frame timing
 * - Core lifecycle (load, run, pause, reset, shutdown)
 * - Save state management
 * - Event loop coordination
 * - Configuration management
 */
class Application {
public:
    Application();
    ~Application();
    
    /**
     * Initialize the application.
     * 
     * @return true on success, false on failure
     */
    bool initialize();
    
    /**
     * Load a ROM file and start emulation.
     * 
     * @param rom_path Path to the ROM file
     * @param core_type Type of core to use ("nds" or "3ds")
     * @return true on success, false on failure
     */
    bool loadROM(const std::string& rom_path, const std::string& core_type);
    
    /**
     * Run the main emulation loop.
     * This will block until the application exits.
     */
    void run();
    
    /**
     * Request the application to exit.
     */
    void requestExit();
    
    /**
     * Check if the application is running.
     */
    bool isRunning() const { return running_; }
    
    /**
     * Pause/unpause emulation.
     */
    void setPaused(bool paused) { paused_ = paused; }
    bool isPaused() const { return paused_; }
    
    /**
     * Reset the current core.
     */
    void reset();
    
    /**
     * Save state to a file.
     * 
     * @param filepath Path to save the state file
     * @return true on success, false on failure
     */
    bool saveState(const std::string& filepath);
    
    /**
     * Load state from a file.
     * 
     * @param filepath Path to the state file
     * @return true on success, false on failure
     */
    bool loadState(const std::string& filepath);
    
    /**
     * Get the current frame rate.
     */
    double getFrameRate() const { return frame_rate_; }
    
    /**
     * Set the target frame rate (for NDS: 60 FPS, for 3DS: variable).
     */
    void setTargetFrameRate(double fps) { target_frame_rate_ = fps; }

private:
    bool running_;
    bool paused_;
    double target_frame_rate_;
    double frame_rate_;
    
    std::unique_ptr<EmulatorCore> core_;
    std::unique_ptr<Frontend> frontend_;
    
    // Timing
    std::chrono::high_resolution_clock::time_point last_frame_time_;
    std::chrono::high_resolution_clock::time_point frame_start_time_;
    
    // Frame timing accumulator for smooth frame pacing
    double frame_time_accumulator_;
    
    /**
     * Process one frame of emulation.
     */
    void processFrame();
    
    /**
     * Update frame rate statistics.
     */
    void updateFrameRate();
    
    /**
     * Handle SDL events.
     */
    void handleEvents();
};
