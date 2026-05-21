#include <iostream>
#include <memory>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_net.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Default window resolution
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;

// RAII aliases with custom deleters
using UniqueWindow = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
using UniqueRenderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;

UniqueWindow window{nullptr, SDL_DestroyWindow};
UniqueRenderer renderer{nullptr, SDL_DestroyRenderer};
bool running = true;

// Main frame iteration loop (compatible with WebAssembly/Emscripten)
void MainIteration() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            running = false;
        } else if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }
    }

    // 1. Clear screen with a beautiful dark slate background
    SDL_SetRenderDrawColor(renderer.get(), 25, 25, 35, 255);
    SDL_RenderClear(renderer.get());

    // ==========================================
    // INSERT YOUR RENDERING / DRAWING CODE HERE
    // ==========================================

    // 2. Present the backbuffer frame to the screen
    SDL_RenderPresent(renderer.get());

#ifdef __EMSCRIPTEN__
    if (!running) {
        emscripten_cancel_main_loop();
        
        // Clean up resources for WebAssembly environment
        renderer.reset();
        window.reset();
        SDLNet_Quit();
        TTF_Quit();
        Mix_CloseAudio();
        IMG_Quit();
        SDL_Quit();
    }
#endif
}

int main(int argc, char* argv[]) {
    // 1. Initialize SDL2 Core subsystems
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL_Init Fail: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 2. Create application window
    window.reset(SDL_CreateWindow(
        "SDL2 Blank Template",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    ));
    if (!window) return -1;

    // 3. Create hardware-accelerated renderer
    renderer.reset(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!renderer) return -1;

    // 4. Initialize SDL2 extension libraries
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    TTF_Init();
    SDLNet_Init();

    std::cout << "All SDL2 Libraries initialized successfully!" << std::endl;

    // 5. Start main application loop
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(MainIteration, 0, 1);
#else
    while (running) {
        MainIteration();
        SDL_Delay(1); // CPU usage limiter
    }

    // Clean up resources for Windows/Desktop environment
    renderer.reset();
    window.reset();
    SDLNet_Quit();
    TTF_Quit();
    Mix_CloseAudio();
    IMG_Quit();
    SDL_Quit();
#endif

    return 0;
}