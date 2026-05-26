#!/bin/bash

echo "====================================================="
echo "Building SDL2 WebAssembly Project via Emscripten..."
echo "====================================================="

if ! command -v emcmake &> /dev/null; then
    echo "emcmake not found in PATH. Scanning common directories for Emscripten SDK..."
    
    emsdk_paths=(
        "$EMSDK"
        "/usr/local/emsdk"
        "$HOME/emsdk"
        "../emsdk"
        "../../emsdk"
        "/opt/emsdk"
    )
    
    activated=false
    for path in "${emsdk_paths[@]}"; do
        if [ -n "$path" ] && [ -f "$path/emsdk_env.sh" ]; then
            echo "Found Emscripten SDK at '$path'. Activating environment..."
            # Source the activation script
            source "$path/emsdk_env.sh"
            
            if command -v emcmake &> /dev/null; then
                activated=true
                break
            fi
        fi
    done
    
    if [ "$activated" = false ]; then
        echo "====================================================="
        echo "ERROR: Emscripten SDK (emsdk) not found or not active!"
        echo "====================================================="
        echo "To compile the WebAssembly version, please install and activate EMSDK:"
        echo "1. Clone the EMSDK repository:"
        echo "   git clone https://github.com/emscripten-core/emsdk.git"
        echo "2. Navigate into emsdk directory and install/activate:"
        echo "   cd emsdk"
        echo "   ./emsdk install 3.1.45"
        echo "   ./emsdk activate 3.1.45"
        echo "3. Activate environment variables in your current shell:"
        echo "   source ./emsdk_env.sh"
        echo "4. Rerun this build script!"
        echo "====================================================="
        exit 1
    fi
fi

echo "1. Running Emcmake Configure..."
if [ -f "build_web/CMakeCache.txt" ]; then
    CURRENT_DIR=$(pwd)
    if ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$CURRENT_DIR" build_web/CMakeCache.txt; then
        echo "Source path changed! Cleaning build_web directory..."
        rm -rf build_web
    fi
fi
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
echo "Then visit: http://localhost:8000/BOOK_TO_GAME.html"
echo "====================================================="
