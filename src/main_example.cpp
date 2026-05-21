#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cmath>
#include <memory>
#include <type_traits>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_net.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Game Constants
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;

// RAII wrappers for SDL2 resources
using UniqueWindow    = std::unique_ptr<SDL_Window,   decltype(&SDL_DestroyWindow)>;
using UniqueRenderer  = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
using UniqueTexture   = std::unique_ptr<SDL_Texture,  decltype(&SDL_DestroyTexture)>;
using UniqueFont      = std::unique_ptr<TTF_Font,     decltype(&TTF_CloseFont)>;
using UniqueChunk     = std::unique_ptr<Mix_Chunk,    decltype(&Mix_FreeChunk)>;
using UniqueUDPSocket = std::unique_ptr<std::remove_pointer<UDPsocket>::type, decltype(&SDLNet_UDP_Close)>;

// Application State Structure
struct AppState {
    UniqueWindow window{nullptr, SDL_DestroyWindow};
    UniqueRenderer renderer{nullptr, SDL_DestroyRenderer};
    UniqueTexture logoTexture{nullptr, SDL_DestroyTexture};
    UniqueFont fontTitle{nullptr, TTF_CloseFont};
    UniqueFont fontUI{nullptr, TTF_CloseFont};
    UniqueChunk soundEffect{nullptr, Mix_FreeChunk};
    UniqueUDPSocket udpSocket{nullptr, SDLNet_UDP_Close};
    
    bool running = true;
    
    // Animation properties
    float logoX = 400.0f;
    float logoY = 300.0f;
    float logoDX = 150.0f; // px per sec
    float logoDY = 120.0f; // px per sec
    float logoAngle = 0.0f;
    int logoWidth = 160;
    int logoHeight = 160;
    
    // Status text
    std::string sdl_status = "SDL2 Core: INIT...";
    std::string img_status = "SDL2_image: INIT...";
    std::string mix_status = "SDL2_mixer: INIT...";
    std::string ttf_status = "SDL2_ttf: INIT...";
    std::string net_status = "SDL2_net: INIT...";
    
    bool sdl_ok = false;
    bool img_ok = false;
    bool mix_ok = false;
    bool ttf_ok = false;
    bool net_ok = false;
    
    Uint32 lastTicks = 0;
} state;

// Simple function to render text helper
void RenderText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, bool center = false) {
    if (!font) return;
    
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return;
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        int w, h;
        SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
        
        SDL_Rect rect;
        rect.x = center ? x - w / 2 : x;
        rect.y = center ? y - h / 2 : y;
        rect.w = w;
        rect.h = h;
        
        SDL_RenderCopy(renderer, texture, nullptr, &rect);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

// Sound triggering helper
void PlaySoundEffect() {
    if (state.soundEffect && state.mix_ok) {
        Mix_PlayChannel(-1, state.soundEffect.get(), 0);
        std::cout << "Sound played successfully!" << std::endl;
    }
}

// Initializing libraries
bool Initialize() {
    state.lastTicks = SDL_GetTicks();
    
    // 1. Initialize SDL2 Core
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        state.sdl_status = "SDL2 Core FAIL: " + std::string(SDL_GetError());
        std::cerr << state.sdl_status << std::endl;
        return false;
    }
    state.sdl_ok = true;
    state.sdl_status = "SDL2 Core: INITIALIZED";
    
    // Create Window
    state.window.reset(SDL_CreateWindow(
        "SDL2 Multiplatform Premium Template",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    ));
    
    if (!state.window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create Renderer
    state.renderer.reset(SDL_CreateRenderer(
        state.window.get(), -1, 
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    ));
    
    if (!state.renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    SDL_SetRenderDrawBlendMode(state.renderer.get(), SDL_BLENDMODE_BLEND);
    
    // 2. Initialize SDL2_image
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) {
        state.img_status = "SDL2_image FAIL: " + std::string(IMG_GetError());
        std::cerr << state.img_status << std::endl;
    } else {
        state.img_ok = true;
        state.img_status = "SDL2_image: INITIALIZED (PNG, JPG)";
        
        // Load logo texture
        state.logoTexture.reset(IMG_LoadTexture(state.renderer.get(), "assets/logo.png"));
        if (!state.logoTexture) {
            std::cerr << "Failed to load logo.png, attempting fallback..." << std::endl;
            // Draw a procedural rectangle if logo.png is missing
        }
    }
    
    // 3. Initialize SDL2_mixer
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        state.mix_status = "SDL2_mixer FAIL: " + std::string(Mix_GetError());
        std::cerr << state.mix_status << std::endl;
    } else {
        state.mix_ok = true;
        state.mix_status = "SDL2_mixer: INITIALIZED";
        
        // Load generated sound effect
        state.soundEffect.reset(Mix_LoadWAV("assets/sound.wav"));
        if (!state.soundEffect) {
            std::cerr << "Failed to load assets/sound.wav: " << Mix_GetError() << std::endl;
        }
    }
    
    // 4. Initialize SDL2_ttf
    if (TTF_Init() == -1) {
        state.ttf_status = "SDL2_ttf FAIL: " + std::string(TTF_GetError());
        std::cerr << state.ttf_status << std::endl;
    } else {
        state.ttf_ok = true;
        state.ttf_status = "SDL2_ttf: INITIALIZED";
        
        // Load fonts
        state.fontTitle.reset(TTF_OpenFont("assets/font.ttf", 36));
        state.fontUI.reset(TTF_OpenFont("assets/font.ttf", 18));
        if (!state.fontTitle || !state.fontUI) {
            std::cerr << "Failed to load assets/font.ttf: " << TTF_GetError() << std::endl;
        }
    }
    
    // 5. Initialize SDL2_net
    if (SDLNet_Init() == -1) {
        state.net_status = "SDL2_net FAIL: " + std::string(SDLNet_GetError());
        std::cerr << state.net_status << std::endl;
    } else {
        state.net_ok = true;
        state.net_status = "SDL2_net: INITIALIZED";
        
        // Open UDP socket on port 9999 as server test
        state.udpSocket.reset(SDLNet_UDP_Open(9999));
        if (state.udpSocket) {
            state.net_status += " (UDP Server listening on port 9999)";
        } else {
            state.net_status += " (No local listener: " + std::string(SDLNet_GetError()) + ")";
        }
    }
    
    return true;
}

// Handle Events
void HandleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                state.running = false;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    state.running = false;
                } else if (event.key.keysym.sym == SDLK_SPACE) {
                    PlaySoundEffect();
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                PlaySoundEffect();
                break;
            case SDL_FINGERDOWN:
                // Support touchscreen tapping for Android/iOS
                PlaySoundEffect();
                break;
        }
    }
}

// Update Game State
void Update(float dt) {
    // 1. Move logo
    state.logoX += state.logoDX * dt;
    state.logoY += state.logoDY * dt;
    
    // 2. Rotate logo
    state.logoAngle += 45.0f * dt; // 45 degrees per second
    if (state.logoAngle >= 360.0f) state.logoAngle -= 360.0f;
    
    // Get actual window size for bouncing bounds (handles resize)
    int w, h;
    SDL_GetWindowSize(state.window.get(), &w, &h);
    
    // Collision detection with window borders
    float halfW = state.logoWidth / 2.0f;
    float halfH = state.logoHeight / 2.0f;
    
    if (state.logoX - halfW < 0) {
        state.logoX = halfW;
        state.logoDX = -state.logoDX;
        PlaySoundEffect();
    } else if (state.logoX + halfW > w) {
        state.logoX = w - halfW;
        state.logoDX = -state.logoDX;
        PlaySoundEffect();
    }
    
    if (state.logoY - halfH < 0) {
        state.logoY = halfH;
        state.logoDY = -state.logoDY;
        PlaySoundEffect();
    } else if (state.logoY + halfH > h) {
        state.logoY = h - halfH;
        state.logoDY = -state.logoDY;
        PlaySoundEffect();
    }
}

// Render everything
void Render() {
    // Premium aesthetics: Deep space dark gradient background
    // Clear screen with fallback solid color
    SDL_SetRenderDrawColor(state.renderer.get(), 10, 10, 20, 255);
    SDL_RenderClear(state.renderer.get());
    
    int w, h;
    SDL_GetWindowSize(state.window.get(), &w, &h);
    
    // Draw subtle procedural gradient
    for (int y = 0; y < h; y += 4) {
        // Linear interpolation between purple-ish and deep blue
        float factor = (float)y / h;
        Uint8 r = (Uint8)(12 * (1.0f - factor) + 8 * factor);
        Uint8 g = (Uint8)(8 * (1.0f - factor) + 12 * factor);
        Uint8 b = (Uint8)(24 * (1.0f - factor) + 38 * factor);
        
        SDL_SetRenderDrawColor(state.renderer.get(), r, g, b, 255);
        SDL_Rect rect = { 0, y, w, 4 };
        SDL_RenderFillRect(state.renderer.get(), &rect);
    }
    
    // Draw neon wire grids
    SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 20); // semi-transparent cyan
    for (int x = 0; x < w; x += 40) {
        SDL_RenderDrawLine(state.renderer.get(), x, 0, x, h);
    }
    for (int y = 0; y < h; y += 40) {
        SDL_RenderDrawLine(state.renderer.get(), 0, y, w, y);
    }
    
    // Render animated logo
    if (state.logoTexture) {
        SDL_Rect destRect;
        destRect.x = (int)(state.logoX - state.logoWidth / 2.0f);
        destRect.y = (int)(state.logoY - state.logoHeight / 2.0f);
        destRect.w = state.logoWidth;
        destRect.h = state.logoHeight;
        
        SDL_RenderCopyEx(
            state.renderer.get(), state.logoTexture.get(), 
            nullptr, &destRect, 
            state.logoAngle, nullptr, SDL_FLIP_NONE
        );
    } else {
        // Procedural neon square fallback
        SDL_Rect destRect;
        destRect.x = (int)(state.logoX - 50);
        destRect.y = (int)(state.logoY - 50);
        destRect.w = 100;
        destRect.h = 100;
        
        SDL_SetRenderDrawColor(state.renderer.get(), 255, 0, 128, 180);
        SDL_RenderFillRect(state.renderer.get(), &destRect);
        SDL_SetRenderDrawColor(state.renderer.get(), 255, 255, 255, 255);
        SDL_RenderDrawRect(state.renderer.get(), &destRect);
    }
    
    // Colors
    SDL_Color colorTitle = { 0, 220, 255, 255 }; // Cyan
    SDL_Color colorLabel = { 200, 200, 220, 255 }; // Soft grey
    SDL_Color colorGreen = { 50, 255, 120, 255 }; // Success Green
    SDL_Color colorRed   = { 255, 60, 100, 255 }; // Fail Red
    
    // Draw Title UI
    RenderText(state.renderer.get(), state.fontTitle.get(), "SDL2 Multiplatform Template", w / 2, 40, colorTitle, true);
    
    // Draw Interactive instructions
    RenderText(state.renderer.get(), state.fontUI.get(), "Press SPACE or CLICK screen to play synthetic retro sound", w / 2, h - 50, colorLabel, true);
    
    // Draw module check list
    int yStart = 120;
    int xStart = 40;
    
    RenderText(state.renderer.get(), state.fontUI.get(), "[ Platform Initialization Status ]", xStart, yStart, colorTitle);
    
    RenderText(state.renderer.get(), state.fontUI.get(), state.sdl_status, xStart + 20, yStart + 35, state.sdl_ok ? colorGreen : colorRed);
    RenderText(state.renderer.get(), state.fontUI.get(), state.img_status, xStart + 20, yStart + 65, state.img_ok ? colorGreen : colorRed);
    RenderText(state.renderer.get(), state.fontUI.get(), state.mix_status, xStart + 20, yStart + 95, state.mix_ok ? colorGreen : colorRed);
    RenderText(state.renderer.get(), state.fontUI.get(), state.ttf_status, xStart + 20, yStart + 125, state.ttf_ok ? colorGreen : colorRed);
    RenderText(state.renderer.get(), state.fontUI.get(), state.net_status, xStart + 20, yStart + 155, state.net_ok ? colorGreen : colorRed);
    
    // Draw bouncing coordinates
    std::stringstream ss;
    ss << "Logo Pos: (" << (int)state.logoX << ", " << (int)state.logoY << ") Angle: " << (int)state.logoAngle << "°";
    RenderText(state.renderer.get(), state.fontUI.get(), ss.str(), xStart, yStart + 210, colorLabel);
    
    SDL_RenderPresent(state.renderer.get());
}

// Clean up
void Cleanup() {
    std::cout << "Cleaning up allocated SDL resources..." << std::endl;
    
    state.udpSocket.reset();
    state.soundEffect.reset();
    state.logoTexture.reset();
    state.fontTitle.reset();
    state.fontUI.reset();
    state.renderer.reset();
    state.window.reset();
    
    if (state.net_ok) SDLNet_Quit();
    if (state.ttf_ok) TTF_Quit();
    if (state.mix_ok) Mix_CloseAudio();
    if (state.img_ok) IMG_Quit();
    if (state.sdl_ok) SDL_Quit();
    
    std::cout << "Cleanup completed successfully!" << std::endl;
}

// Core Loop Iteration
void MainIteration() {
    Uint32 currentTicks = SDL_GetTicks();
    float dt = (currentTicks - state.lastTicks) / 1000.0f;
    state.lastTicks = currentTicks;
    
    // Cap DT to prevent massive teleports during lag spikes
    if (dt > 0.1f) dt = 0.1f;
    
    HandleEvents();
    Update(dt);
    Render();
    
#ifdef __EMSCRIPTEN__
    if (!state.running) {
        emscripten_cancel_main_loop();
        Cleanup();
    }
#endif
}

// Entry Point
int main(int argc, char* argv[]) {
    // Call Initialize
    if (!Initialize()) {
        std::cerr << "Initialization failed! Aborting." << std::endl;
        Cleanup();
        return -1;
    }
    
    std::cout << "Initialization successful! Starting main loop." << std::endl;
    
#ifdef __EMSCRIPTEN__
    // Emscripten demands setting loop callback instead of infinite while loop
    emscripten_set_main_loop(MainIteration, 0, 1);
#else
    // Desktop platform standard loop
    while (state.running) {
        MainIteration();
        
        // Add tiny sleep to limit CPU usage on systems without vsync
        SDL_Delay(1);
    }
    Cleanup();
#endif
    
    return 0;
}
