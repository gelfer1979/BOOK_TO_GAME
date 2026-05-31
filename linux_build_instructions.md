<!--  --># Cross-Compilation & Testing Linux (Ubuntu 20.04) Builds on Windows

To compile and test native Linux targets (`BOOK_TO_GAME` and `NativeWebViewWrapper`) for Ubuntu 20.04 directly on Windows, the most robust, industry-standard, and recommended solution is using **WSL2 (Windows Subsystem for Linux)** with **WSLg (WSL Graphical Support)**. 

This gives you a fully functional, authentic native Ubuntu 20.04 compiler toolchain and lets you run graphical Linux applications (GTK, SDL2, and WebKit WebView) side-by-side with your Windows apps.

---

## Step 1: Install and Configure WSL2 (Ubuntu 20.04)

1. Open **PowerShell** as Administrator and run the following command to install WSL and Ubuntu 20.04:
   ```powershell
   wsl --install -d Ubuntu-20.04
   ```
2. Restart your computer if prompted.
3. Open the newly installed **Ubuntu 20.04** terminal from your Start Menu, set up your username and password, and update the package list:
   ```bash
   sudo apt update && sudo apt upgrade -y
   ```

---

## Step 2: Install Linux C++ Development Dependencies

Install CMake, GNU compiler collections (GCC/G++), and dev packages for SDL2, Curl, GTK3, and WebKitGTK:

```bash
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    libsdl2-dev \
    libsdl2-image-dev \
    libsdl2-mixer-dev \
    libsdl2-ttf-dev \
    libsdl2-net-dev \
    libcurl4-openssl-dev \
    libwebkit2gtk-4.0-dev \
    libgtk-3-dev
```

---

## Step 3: Access your Windows Codebase in WSL2

WSL2 automatically mounts your Windows drives under `/mnt/`. Navigate to your project directory inside the Ubuntu terminal:

```bash
cd /mnt/c/games/BOOK_TO_GAME
```

---

## Step 4: Configure and Compile the Linux Target

Configure CMake and compile both the native SDL2 game (`BOOK_TO_GAME`) and the GTK WebKit WebView wrapper (`NativeWebViewWrapper`):

```bash
# 1. Create a clean Linux build folder
cmake -B build_linux -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 2. Compile the native SDL2 game
cmake --build build_linux --target BOOK_TO_GAME

# 3. Compile the native Linux WebView Wrapper
cmake --build build_linux --target NativeWebViewWrapper
```

---

## Step 5: Test and Run Graphical Linux Apps on Windows (WSLg)

WSL2 automatically configures X11/Wayland display routing (WSLg) on modern Windows 10/11 systems. You can launch your compiled Linux graphical binaries directly from your Ubuntu terminal, and they will render in beautiful, hardware-accelerated windows on your Windows desktop!

### A. Run the Native C++ SDL2 Game:
```bash
./build_linux/BOOK_TO_GAME
```

### B. Run the Native WebKitGTK WebView Wrapper:
```bash
# Start your local server first (or let the app hit localhost)
python3 -m http.server 8000 --directory build_web &

# Run the WebView Wrapper
./build_linux/NativeWebViewWrapper
```

---

## Alternative: Headless Docker Cross-Compilation (No WSL)

If you prefer using **Docker** for headless cross-compilation without WSL, you can spin up an Ubuntu 20.04 container, mount the folder, and run compilation:

```powershell
# 1. Run the compilation container
docker run --rm -v ${PWD}:/workspace -w /workspace ubuntu:20.04 bash -c "
    apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake ninja-build libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev libsdl2-net-dev libwebkit2gtk-4.0-dev libgtk-3-dev &&
    cmake -B build_linux -G Ninja &&
    cmake --build build_linux
"
```
*Note: To test/run the resulting graphical binary from Docker, you will need to install an X server on Windows (like VcXsrv) and pass the `DISPLAY` environment variable to the docker container.*
