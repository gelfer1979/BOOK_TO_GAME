#!/bin/bash

echo "====================================================="
echo "Building SDL2 WebAssembly Project via Emscripten..."
echo "====================================================="

if [ -z "$EMSDK" ]; then
    echo "WARNING: EMSDK environment variable is not detected!"
    echo "Please ensure Emscripten SDK is installed and activated (run 'source emsdk_env.sh')."
    echo "Attempting build anyway..."
fi

echo "1. Running Emcmake Configure..."
emcmake cmake -B build_web -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo "ERROR: CMake configuration failed!"
    exit 1
fi

echo "2. Building Project..."
cmake --build build_web --config Release

if [ $? -ne 0 ]; then
    echo "ERROR: Compilation failed!"
    exit 1
fi

echo -e "\n====================================================="
echo "SUCCESS: WebAssembly Build Completed Successfully!"
echo "Output files are located in: platforms/emscripten/build_web/"
echo "====================================================="
echo "NOTE: WebAssembly files cannot be opened directly from the disk due to CORS."
echo "You must host them using a local web server."
echo "To host it, run this command:"
echo "  python3 -m http.server 8000 --directory build_web"
echo "Then visit: http://localhost:8000/sdl2_template.html"
echo "====================================================="
