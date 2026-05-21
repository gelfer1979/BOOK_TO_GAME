# Multiplatform SDL2 C++ Project Template

This repository contains a professionally configured, fully ready-to-develop C++ application template built on top of **SDL2** and its official extension libraries:
*   **SDL2 (Core)** — System initialization, window management, and input event handling.
*   **SDL2_image** — Loading and rendering 2D raster graphics (PNG, JPG, etc.).
*   **SDL2_mixer** — Playing music and spatial/synthetic sound effects.
*   **SDL2_ttf** — Loading and anti-aliased rendering of TrueType fonts.
*   **SDL2_net** — Network communications via TCP and UDP sockets.

## Supported Platforms

1.  **Windows** (Compilers: Visual Studio MSVC or MinGW GCC) — Via precompiled libraries.
2.  **macOS** (Desktop) — Via Homebrew system dependencies.
3.  **Linux** — Via system package manager dependencies.
4.  **Android** — A complete Android Studio project (Gradle) compiling SDL2 libraries directly from source.
5.  **iOS** — Xcode project generation via CMake, compiling all SDL2 libraries directly from source.
6.  **Emscripten (WebAssembly)** — Compilation to run your game directly inside any modern web browser.

---

## Project Structure

```
├── .vscode/                               # Integration configurations for VS Code
│   ├── c_cpp_properties.json              # IntelliSense paths (perfect autocomplete without errors)
│   ├── tasks.json                         # Automated compilation scripts (Ctrl+Shift+B)
│   └── launch.json                        # Debug profiles for F5 execution (GDB integration)
│
├── assets/                                # Game assets (created automatically via setup.py)
│   ├── logo.png                           # Glowing neon AI-generated template logo
│   ├── font.ttf                           # Roboto font (fetched from Google Fonts)
│   └── sound.wav                          # Procedural 8-bit chiptune sound (synthesized dynamically)
│
├── external/                              # Third-party dependencies (managed via setup.py)
│   ├── vc/                                # Precompiled headers/binaries for MSVC compiler (x64/x86)
│   ├── mingw/                             # Precompiled headers/binaries for MinGW compiler (x64/x86)
│   └── src/                               # SDL2 library source codes for Android/iOS source compilation
│
├── platforms/                             # Mobile and web build environments
│   ├── android/                           # Complete Android Studio project (Gradle configuration)
│   └── emscripten/                        # WebAssembly build scripts (build_web.sh / build_web.ps1)
│
├── src/                                   # Application source code
│   └── main.cpp                           # Main source file implementing the multiplatform game loop
│
├── CMakeLists.txt                         # Unified cross-platform CMake build configuration
├── setup.py                               # Automation script to fetch dependencies and generate assets
└── README.md                              # This manual
```

---

## Step 1: Automatic Dependency Setup

Before you start building, you need to download the required libraries and assets. The repository includes an automated Python script `setup.py` that handles everything.

Run it from your terminal in the project root:
```bash
python setup.py
```

**What the script does:**
1.  Downloads official prebuilt Windows developer libraries for **MSVC** and **MinGW**.
2.  Fetches complete **source code** archives for all five SDL2 libraries for mobile compilation.
3.  Locates the official `SDLActivity.java` helper class and copies it into the Android Gradle structure.
4.  Downloads the clean `Roboto-Regular` font from Google Fonts and saves it as `assets/font.ttf`.
5.  Synthesizes a retro 8-bit coin-pickup sound effect and saves it as `assets/sound.wav`.

---

## Step 2: Building and Running

> [!TIP]
> **Unified VS Code Integration:** You can configure, compile, and build this project **directly inside Visual Studio Code** for almost all target platforms:
> *   **Windows** (via `Build Windows (Ninja debug)` / `Build Windows (Ninja release)`)
> *   **macOS** (via `Build macOS (Debug)` / `Build macOS (Release)`)
> *   **Linux** (via `Build Linux (Debug)` / `Build Linux (Release)`)
> *   **WebAssembly / Emscripten** (via `Build Web (Emscripten)` — works on Windows, macOS, and Linux!)
> *   **iOS / Simulator** (via `Generate iOS Xcode Project` — generates the Xcode project build directory directly from VS Code on macOS)
> 
> Simply open the Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`), select **Tasks: Run Task**, and choose your desired configuration. This completely eliminates the need to run raw terminal commands or switch tools!

### 1. Windows (via VS Code)

The template is fully pre-configured for **VS Code** out of the box!

1.  Install the official **C/C++** and **CMake Tools** extensions in VS Code.
2.  Open the project root directory in VS Code.
3.  Press `Ctrl+Shift+B` to automatically run CMake configuration (via Ninja) and compile the binary.
4.  Press **F5** to start full interactive debugging! 
5.  *Note:* The `CMakeLists.txt` script will automatically copy all required `.dll` files and the `assets` folder into your output build directory. Everything works in one click!

### 2. Android (via Android Studio)

The Android platform builds all libraries from source using the NDK, avoiding any potential ABI mismatch conflicts.

1.  Install Android Studio and the Android NDK via the SDK Manager.
2.  Open the `platforms/android` directory in Android Studio.
3.  Android Studio will automatically detect the Gradle files, initialize CMake, and link the root `CMakeLists.txt`.
4.  Connect your physical device or launch an emulator and click the **Run** button. It will compile SDL2 and launch your app.

### 3. Emscripten (WebAssembly)

1.  Install and activate the **Emscripten SDK** (`emsdk`).
2.  Open your terminal in `platforms/emscripten`.
3.  Execute the build script:
    *   On Windows (PowerShell): `./build_web.ps1`
    *   On macOS / Linux (Bash): `bash build_web.sh`
4.  The output files (including `sdl2_template.html`) will be generated inside `platforms/emscripten/build_web/`.
5.  **Running:** Modern browsers will block WebAssembly files opened directly from disk due to CORS policies. Start a local server:
    ```bash
    python -m http.server 8000 --directory build_web
    ```
    Then visit: `http://localhost:8000/sdl2_template.html` in your browser.

### 4. iOS (Xcode)

To target iOS devices and simulators on macOS, use the built-in Xcode generator in CMake:

1.  Open your terminal in the project root.
2.  Generate the iOS Xcode project:
    ```bash
    cmake -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
    ```
3.  Open the newly created `build-ios/sdl2_template.xcodeproj` in Xcode.
4.  Configure your **Signing & Capabilities** credentials (select your Apple ID / Development Team).
5.  Select your target iOS device or simulator and press **Run (Cmd+R)**.

### 5. macOS (Desktop)

The template fully supports native macOS desktop builds via standard CMake and Clang:

1.  Install required SDL2 libraries using **Homebrew**:
    ```bash
    brew install sdl2 sdl2_image sdl2_mixer sdl2_ttf sdl2_net
    ```
2.  Open the project in VS Code.
3.  Open the Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`) -> select **Tasks: Run Task** -> select **`Build macOS (Release)`**.
4.  Alternatively, compile the binary via terminal:
    ```bash
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ```
5.  Execute the compiled application: `./build/your_project_name`

### 6. Linux

1.  Install the SDL2 development package files via your system's package manager.
    *   **Ubuntu / Debian:**
        ```bash
        sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev libsdl2-net-dev
        ```
    *   **Arch Linux:**
        ```bash
        sudo pacman -S sdl2 sdl2_image sdl2_mixer sdl2_ttf sdl2_net
        ```
2.  Configure and compile the project using CMake:
    ```bash
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ```
3.  Execute your compiled app: `./build/your_project_name`
