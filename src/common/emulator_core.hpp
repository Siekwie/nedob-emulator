#pragma once

#include <string>
#include <SDL3/SDL_events.h>

// Forward declaration
class Frontend;

/**
 * Base interface for emulator cores (NDS, 3DS, etc.).
 * 
 * Each core implements this interface to provide a consistent
 * API for the application layer.
 */
class EmulatorCore {
public:
    virtual ~EmulatorCore() = default;
    
    /**
     * Load a ROM file into the core.
     * 
     * @param rom_path Path to the ROM file
     * @return true on success, false on failure
     */
    virtual bool loadROM(const std::string& rom_path) = 0;
    
    /**
     * Reset the core to its initial state.
     */
    virtual void reset() = 0;
    
    /**
     * Run the core for one frame.
     * This should advance emulation by one display frame.
     */
    virtual void runFrame() = 0;
    
    /**
     * Handle an SDL event (input, etc.).
     * 
     * @param event The SDL event
     */
    virtual void handleEvent(const SDL_Event& event) = 0;
    
    /**
     * Set the frontend for rendering and audio output.
     * 
     * @param frontend Pointer to the frontend instance
     */
    virtual void setFrontend(Frontend* frontend) = 0;
    
    /**
     * Save the current state to a file.
     * 
     * @param filepath Path to save the state file
     * @return true on success, false on failure
     */
    virtual bool saveState(const std::string& filepath) = 0;
    
    /**
     * Load a state from a file.
     * 
     * @param filepath Path to the state file
     * @return true on success, false on failure
     */
    virtual bool loadState(const std::string& filepath) = 0;
    
    /**
     * Get the core's name (e.g., "NDS", "3DS").
     */
    virtual const char* getName() const = 0;
};
