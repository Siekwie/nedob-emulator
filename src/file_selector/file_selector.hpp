#pragma once

#include "rom_type.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_video.h>

#include <functional>
#include <string>
#include <vector>

struct RomSelectionResult {
    std::string filepath{};
    RomType rom_type{RomType::Unknown};
    bool cancelled{false};
    bool error{false};
};

class FileSelector {
public:
    explicit FileSelector(SDL_Window* window);
    ~FileSelector();

    FileSelector(const FileSelector&) = delete;
    FileSelector& operator=(const FileSelector&) = delete;
    FileSelector(FileSelector&&) = delete;
    FileSelector& operator=(FileSelector&&) = delete;

    void setWindow(SDL_Window* window);

    // Asynchronous: callback will be invoked by SDL after selection/cancel.
    void showDialog(std::function<void(const RomSelectionResult&)> callback,
                    const char* default_path = nullptr);

    // Synchronous helper: pumps events until done.
    RomSelectionResult showDialogSync(const char* default_path = nullptr);

private:
    std::vector<SDL_DialogFileFilter> getRomFilters();
    static void dialogCallback(void* userdata, const char* const* filelist, int filter);
    void processDialogResult(const char* const* filelist, int filter,
                             std::function<void(const RomSelectionResult&)> callback);

    SDL_Window* window_{nullptr};
    bool dialog_active_{false};
    std::vector<SDL_DialogFileFilter> filters_{};
};

