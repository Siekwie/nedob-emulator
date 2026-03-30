#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include "../file_selector/file_selector.hpp"
#include "../file_selector/rom_type.hpp"
#include "application.hpp"
#include "logger.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

int main(int argc, char* argv[])
{
    constexpr const char* kDevRomPath = "/home/siekwie/Documents/ROMs/3DS/PokemonSun/decrypted/PokemonSun.3ds";
    constexpr bool kUseDevRomPath = true;

    SDL_SetMainReady();
    Logger::init();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        const char* error = SDL_GetError();
        if (error && error[0] != '\0') {
            Logger::log("SDL_Init failed: %s\n", error);
        } else {
            Logger::log("SDL_Init failed: Unknown error (error string was empty)\n");
        }
        return 1;
    }

    // Create a minimal window for the file selector dialog
    // The window can be hidden or minimized, but SDL dialogs may need it
    SDL_Window* window = SDL_CreateWindow(
        "Nedob Emulator - ROM Selector",
        800, 600,
        SDL_WINDOW_HIDDEN  // Start hidden, we only need it for the dialog
    );
    
    if (!window) {
        Logger::log("Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    RomSelectionResult result;
    bool rom_selected = false;

    if (kUseDevRomPath && std::filesystem::exists(kDevRomPath)) {
        // Bringup defaults for the hardcoded dev ROM path. These can still be overridden by
        // explicitly setting environment variables before launching the emulator.
        if (std::getenv("NEDOB_BREAK_SPINS") == nullptr) {
            ::setenv("NEDOB_BREAK_SPINS", "1", 1);
        }
        if (std::getenv("NEDOB_SPIN_THRESHOLD") == nullptr) {
            ::setenv("NEDOB_SPIN_THRESHOLD", "20000", 1);
        }
        if (std::getenv("NEDOB_PATCH_PANIC_LOOP") == nullptr) {
            ::setenv("NEDOB_PATCH_PANIC_LOOP", "1", 1);
        }
        if (std::getenv("NEDOB_LOG_PANIC_STRING") == nullptr) {
            ::setenv("NEDOB_LOG_PANIC_STRING", "1", 1);
        }
        if (std::getenv("NEDOB_MAX_FRAMES") == nullptr) {
            ::setenv("NEDOB_MAX_FRAMES", "60", 1);
        }
        if (std::getenv("NEDOB_SKIP_NONEXEC_CALLS") == nullptr) {
            ::setenv("NEDOB_SKIP_NONEXEC_CALLS", "1", 1);
        }
        if (std::getenv("NEDOB_BOOTSTRAP_THREADS") == nullptr) {
            ::setenv("NEDOB_BOOTSTRAP_THREADS", "1", 1);
        }
        if (std::getenv("NEDOB_THREAD_BOOTSTRAP_STEPS") == nullptr) {
            ::setenv("NEDOB_THREAD_BOOTSTRAP_STEPS", "2000", 1);
        }
        if (std::getenv("NEDOB_PATCH_BLX_2E7310_REDIRECT") == nullptr) {
            ::setenv("NEDOB_PATCH_BLX_2E7310_REDIRECT", "1", 1);
        }
        if (std::getenv("NEDOB_PATCH_WORKER_READY_CHECK") == nullptr) {
            ::setenv("NEDOB_PATCH_WORKER_READY_CHECK", "1", 1);
        }
        if (std::getenv("NEDOB_PATCH_WORKER_LIST") == nullptr) {
            ::setenv("NEDOB_PATCH_WORKER_LIST", "1", 1);
        }
        if (std::getenv("NEDOB_PATCH_WORKER_LOOP_BNE") == nullptr) {
            ::setenv("NEDOB_PATCH_WORKER_LOOP_BNE", "1", 1);
        }
        if (std::getenv("NEDOB_PATCH_WORKER_RET1") == nullptr) {
            ::setenv("NEDOB_PATCH_WORKER_RET1", "1", 1);
        }
        if (std::getenv("NEDOB_PATCH_MAIN_LOOP_EXIT") == nullptr) {
            ::setenv("NEDOB_PATCH_MAIN_LOOP_EXIT", "1", 1);
        }
        // NEDOB_PATCH_TIMING_CHECK=1 skips panic path but leads to thread bootstrap spam; off by default

        result.filepath = kDevRomPath;
        result.rom_type = RomType::ThreeDS;
        result.cancelled = false;
        result.error = false;
        rom_selected = true;
        Logger::logInfo("Nedob Emulator - Development ROM mode\n");
        Logger::logInfo("Using hardcoded ROM: %s\n", kDevRomPath);
    } else {
        Logger::logInfo("Nedob Emulator - ROM Selector\n");
        Logger::logInfo("Select a ROM file to load...\n");

        // Create file selector
        // file selector will load rom into emulator
        // upon loading rom, selector closes
        // emulator will load rom with correct core
        FileSelector selector(window);

        // Show the file dialog
        selector.showDialog([&result, &rom_selected](const RomSelectionResult& res) {
            result = res;
            rom_selected = true;
        });
    }

    // Main event loop - wait for file selection or quit
    bool running = true;
    SDL_Event event;

    while (running && !rom_selected) {
        // Poll for events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
                result.cancelled = true;
                rom_selected = true;
            }
        }

        // Small delay to prevent 100% CPU usage
        SDL_Delay(16); // ~60 FPS
    }

    // Process the ROM selection result
    if (result.error) {
        Logger::log("Error selecting ROM file: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    } else if (result.cancelled) {
        Logger::logInfo("ROM selection cancelled.\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } else if (!result.filepath.empty()) {
        Logger::logInfo("Selected ROM: %s\n", result.filepath.c_str());
        Logger::logInfo("ROM Type: %s\n", romTypeToString(result.rom_type));
        
        // Close the selector window - we'll create a new one in the application
        SDL_DestroyWindow(window);
        
        // Determine core type based on ROM type
        std::string core_type;
        switch (result.rom_type) {
            case RomType::ThreeDS:
                core_type = "3ds";
                break;
            default:
                Logger::log("Warning: Unknown ROM type, attempting 3DS core\n");
                core_type = "3ds";
                break;
        }
        
        // Create and initialize the application
        Application app;
        if (!app.initialize()) {
            Logger::log("Failed to initialize application\n");
            SDL_Quit();
            return 1;
        }
        
        if (!app.loadROM(result.filepath, core_type)) {
            Logger::log("Failed to load ROM\n");
            SDL_Quit();
            return 1;
        }
        
        Logger::logInfo("Starting emulation...\n");
        app.run();
        Logger::logInfo("Emulation ended.\n");
    }

    Logger::shutdown();
    SDL_Quit();
    return 0;
}
