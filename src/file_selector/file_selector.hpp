#pragma once

#include "rom_type.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <string>
#include <functional>
#include <memory>
#include <vector>

/**
 * Result structure returned when a ROM file is selected.
 */
struct RomSelectionResult {
    std::string filepath;      // Full path to the selected ROM file
    RomType rom_type;           // Detected ROM type
    bool cancelled;             // True if user cancelled the dialog
    bool error;                 // True if an error occurred
    
    RomSelectionResult() : rom_type(RomType::Unknown), cancelled(false), error(false) {}
};

/**
 * File selector class that provides a UI for selecting ROM files.
 * 
 * Uses SDL3's native file dialog to present a platform-appropriate
 * file picker. Automatically detects ROM type from file extension.
 */
class FileSelector {
public:
    /**
     * Constructor.
     * 
     * @param window The SDL window to attach the dialog to (can be nullptr)
     */
    explicit FileSelector(SDL_Window* window = nullptr);
    
    /**
     * Destructor.
     */
    ~FileSelector();
    
    /**
     * Shows the file selection dialog asynchronously.
     * 
     * The dialog will filter for supported ROM file types (.nds, .3ds, .cia).
     * When the user selects a file or cancels, the callback will be invoked.
     * 
     * @param callback Function to call when selection is complete
     * @param default_path Optional default directory to open the dialog in
     */
    void showDialog(std::function<void(const RomSelectionResult&)> callback,
                    const char* default_path = nullptr);
    
    /**
     * Shows the file selection dialog synchronously (blocks until user responds).
     * 
     * Note: This is a convenience wrapper that uses SDL events to wait
     * for the async dialog to complete. The main event loop must be running.
     * 
     * @param default_path Optional default directory to open the dialog in
     * @return The selection result
     */
    RomSelectionResult showDialogSync(const char* default_path = nullptr);
    
    /**
     * Sets the window to attach dialogs to.
     * 
     * @param window The SDL window (can be nullptr)
     */
    void setWindow(SDL_Window* window);
    
    /**
     * Checks if a dialog is currently showing.
     * 
     * @return True if a dialog is active
     */
    bool isDialogActive() const { return dialog_active_; }

private:
    SDL_Window* window_;
    bool dialog_active_;
    std::vector<SDL_DialogFileFilter> filters_;  // Keep filters alive until callback
    
    // Internal callback handler for SDL dialog
    static void dialogCallback(void* userdata, const char* const* filelist, int filter);
    
    // Helper to process dialog result
    void processDialogResult(const char* const* filelist, int filter,
                            std::function<void(const RomSelectionResult&)> callback);
    
    // File filters for supported ROM types
    static std::vector<SDL_DialogFileFilter> getRomFilters();
};
