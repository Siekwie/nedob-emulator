#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <cstdio>

int main(int argc, char* argv[])
{
    // Initialize SDL with video subsystem
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create a window
    SDL_Window* window = SDL_CreateWindow(
        "Nedob Emulator",
        800,  // width
        600,  // height
        0     // flags (0 = default)
    );

    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    std::printf("Window created successfully!\n");
    std::printf("Press ESC or close the window to exit.\n");

    // Main event loop
    bool running = true;
    SDL_Event event;

    while (running) {
        // Poll for events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                // Exit on ESC key
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
            }
        }

        // Small delay to prevent 100% CPU usage
        SDL_Delay(16); // ~60 FPS
    }

    // Cleanup
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
