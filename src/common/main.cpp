#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include "../file_selector/file_selector.hpp"
#include "../file_selector/rom_type.hpp"
#include "application.hpp"
#include <cstdio>
#include <string>

int main(int argc, char* argv[])
{
    // Tell SDL we're handling the main function ourselves
    SDL_SetMainReady();
    
    // Initialize SDL with video subsystem
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        const char* error = SDL_GetError();
        if (error && error[0] != '\0') {
            std::fprintf(stderr, "SDL_Init failed: %s\n", error);
        } else {
            std::fprintf(stderr, "SDL_Init failed: Unknown error (error string was empty)\n");
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
        std::fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    std::printf("Nedob Emulator - ROM Selector\n");
    std::printf("Select a ROM file to load...\n");

    // Create file selector
    // file selector will load rom into emulator
    // upon loading rom, selector closes
    // emulator will load rom with correct core
    FileSelector selector(window);
    
    RomSelectionResult result;
    bool rom_selected = false;
    
    // Show the file dialog
    selector.showDialog([&result, &rom_selected](const RomSelectionResult& res) {
        result = res;
        rom_selected = true;
    });

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
        std::fprintf(stderr, "Error selecting ROM file: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    } else if (result.cancelled) {
        std::printf("ROM selection cancelled.\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    } else if (!result.filepath.empty()) {
        std::printf("Selected ROM: %s\n", result.filepath.c_str());
        std::printf("ROM Type: %s\n", romTypeToString(result.rom_type));
        
        // Close the selector window - we'll create a new one in the application
        SDL_DestroyWindow(window);
        
        // Determine core type based on ROM type
        std::string core_type;
        switch (result.rom_type) {
            case RomType::NDS:
            case RomType::NDSi:
                core_type = "nds";
                break;
            case RomType::ThreeDS:
                core_type = "3ds";
                break;
            default:
                std::fprintf(stderr, "Warning: Unknown ROM type, attempting NDS core\n");
                core_type = "nds";
                break;
        }
        
        // Create and initialize the application
        Application app;
        if (!app.initialize()) {
            std::fprintf(stderr, "Failed to initialize application\n");
            SDL_Quit();
            return 1;
        }
        
        // Load the ROM
        if (!app.loadROM(result.filepath, core_type)) {
            std::fprintf(stderr, "Failed to load ROM\n");
            SDL_Quit();
            return 1;
        }
        
        // Run the emulator
        std::printf("Starting emulation...\n");
        std::printf("Controls:\n");
        std::printf("  ESC - Exit\n");
        std::printf("  P - Pause/Resume\n");
        std::printf("  Ctrl+R - Reset\n");
        std::printf("\n");
        
        app.run();
        
        std::printf("Emulation ended.\n");
    }

    // Cleanup
    SDL_Quit();

    return 0;
}
