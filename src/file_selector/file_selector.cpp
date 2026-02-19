#include "file_selector.hpp"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <cstring>
#include <algorithm>
#include <memory>

namespace {
    // Internal structure to pass callback data to SDL dialog
    struct CallbackData {
        FileSelector* selector;
        std::function<void(const RomSelectionResult&)> user_callback;
    };
}

FileSelector::FileSelector(SDL_Window* window)
    : window_(window), dialog_active_(false) {
}

FileSelector::~FileSelector() {
    // Dialog should be cleaned up by SDL
}

void FileSelector::setWindow(SDL_Window* window) {
    window_ = window;
}

std::vector<SDL_DialogFileFilter> FileSelector::getRomFilters() {
    std::vector<SDL_DialogFileFilter> filters;
    
    // All supported ROM files
    filters.push_back({ "All Supported ROMs", "nds;3ds;cci;cia;cxi;app;3dsx" });
    
    // Individual filters
    filters.push_back({ "Nintendo DS ROMs", "nds" });
    filters.push_back({ "Nintendo 3DS ROMs", "3ds;cci;cia;cxi;app;3dsx" });
    
    // All files (for advanced users)
    filters.push_back({ "All Files", "*" });
    
    return filters;
}

void FileSelector::showDialog(std::function<void(const RomSelectionResult&)> callback,
                              const char* default_path) {
    if (dialog_active_) {
        // Already showing a dialog, ignore
        RomSelectionResult result;
        result.error = true;
        callback(result);
        return;
    }
    
    dialog_active_ = true;
    
    // Get file filters and store them as member to keep them alive until callback
    filters_ = getRomFilters();
    
    // Create callback data structure
    auto cb_data = std::make_unique<CallbackData>();
    cb_data->selector = this;
    cb_data->user_callback = std::move(callback);
    
    // Show the file dialog
    // Note: filters_ must remain valid until the callback is invoked
    SDL_ShowOpenFileDialog(
        dialogCallback,
        cb_data.release(),  // callback takes ownership and deletes it
        window_,
        filters_.data(),
        static_cast<int>(filters_.size()),
        default_path,
        false  // allow_many = false (single file selection)
    );
}

RomSelectionResult FileSelector::showDialogSync(const char* default_path) {
    RomSelectionResult result;
    bool completed = false;
    
    showDialog([&result, &completed](const RomSelectionResult& res) {
        result = res;
        completed = true;
    }, default_path);
    
    // Wait for dialog to complete by pumping events
    // Note: This requires the main event loop to be running
    while (!completed) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Process events to allow dialog callbacks to fire
            if (event.type == SDL_EVENT_QUIT) {
                result.cancelled = true;
                completed = true;
                break;
            }
        }
        SDL_Delay(16); // Small delay to prevent 100% CPU
    }
    
    return result;
}

void FileSelector::dialogCallback(void* userdata, const char* const* filelist, int filter) {
    // Take ownership so we always free the callback data.
    std::unique_ptr<CallbackData> cb_data(static_cast<CallbackData*>(userdata));
    FileSelector* sel = cb_data->selector;
    sel->processDialogResult(filelist, filter, std::move(cb_data->user_callback));
    sel->dialog_active_ = false;
}

void FileSelector::processDialogResult(const char* const* filelist, int filter,
                                      std::function<void(const RomSelectionResult&)> callback) {
    RomSelectionResult result;
    
    // Check for errors
    if (filelist == nullptr) {
        result.error = true;
        callback(result);
        // Clear filters now that callback is done
        filters_.clear();
        return;
    }
    
    // Check if user cancelled (empty selection)
    if (filelist[0] == nullptr) {
        result.cancelled = true;
        callback(result);
        // Clear filters now that callback is done
        filters_.clear();
        return;
    }
    
    // Get the first selected file
    result.filepath = filelist[0];
    
    // Detect ROM type from file extension
    result.rom_type = detectRomType(result.filepath);
    
    callback(result);
    // Clear filters now that callback is done
    filters_.clear();
}
