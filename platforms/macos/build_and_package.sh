#!/bin/bash
# build_and_package.sh - Builds and packages macOS app bundle exactly like GitHub Actions
# Usage: bash platforms/macos/build_and_package.sh

set -e

PROJECT_ROOT="$(pwd)"

echo "=== 1. Checking Dependencies ==="
if ! command -v ninja &> /dev/null; then
    echo "Ninja is not installed. Installing it via Homebrew..."
    brew install ninja
fi

echo "=== 2. Configuring CMake (Universal arm64+x86_64, Ninja generator, Release build) ==="
# Clean build folder if generator/source directory changed to avoid CMake mismatch errors
if [ -f build/CMakeCache.txt ]; then
    if ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$PROJECT_ROOT" build/CMakeCache.txt; then
        echo "Source path changed! Cleaning build directory..."
        rm -rf build
    fi
fi

cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"

echo "=== 3. Compiling and Linking ==="
cmake --build build --config Release

echo "=== 4. Packaging Distributable (.app Bundle) ==="
mkdir -p dist

# 1. Copy the .app bundle structure if it exists, or create a clean one
if [ -d "build/BOOK_TO_GAME.app" ]; then
    cp -r build/BOOK_TO_GAME.app dist/
else
    mkdir -p dist/BOOK_TO_GAME.app/Contents/MacOS
    mkdir -p dist/BOOK_TO_GAME.app/Contents/Resources
fi

# Ensure Contents/Resources exists
mkdir -p dist/BOOK_TO_GAME.app/Contents/Resources

# Copy all root assets to Contents/Resources (guaranteeing all game files are present)
cp -r assets dist/BOOK_TO_GAME.app/Contents/Resources/
# Merge any build-specific assets (like puter_bridge.html) if they exist
cp -r build/assets/* dist/BOOK_TO_GAME.app/Contents/Resources/assets/ 2>/dev/null || true

cp nbook_*.txt dist/BOOK_TO_GAME.app/Contents/Resources/ 2>/dev/null || true
cp book.json dist/BOOK_TO_GAME.app/Contents/Resources/ 2>/dev/null || true
cp ai_*.json dist/BOOK_TO_GAME.app/Contents/Resources/ 2>/dev/null || true
cp options.json dist/BOOK_TO_GAME.app/Contents/Resources/ 2>/dev/null || true
cp settings.json dist/BOOK_TO_GAME.app/Contents/Resources/ 2>/dev/null || true

# Ensure Contents/MacOS exists
mkdir -p dist/BOOK_TO_GAME.app/Contents/MacOS

# Copy all root assets to Contents/MacOS
cp -r assets dist/BOOK_TO_GAME.app/Contents/MacOS/
# Merge any build-specific assets if they exist
cp -r build/assets/* dist/BOOK_TO_GAME.app/Contents/MacOS/assets/ 2>/dev/null || true

cp nbook_*.txt dist/BOOK_TO_GAME.app/Contents/MacOS/ 2>/dev/null || true
cp book.json dist/BOOK_TO_GAME.app/Contents/MacOS/ 2>/dev/null || true
cp ai_*.json dist/BOOK_TO_GAME.app/Contents/MacOS/ 2>/dev/null || true
cp options.json dist/BOOK_TO_GAME.app/Contents/MacOS/ 2>/dev/null || true
cp settings.json dist/BOOK_TO_GAME.app/Contents/MacOS/ 2>/dev/null || true

# 2. Locate the compiled executable binary anywhere in the build tree and copy it in
BINARY_SOURCE=$(find build -type f -name "BOOK_TO_GAME" 2>/dev/null | grep -v "dist/" | head -n 1)
if [ -z "$BINARY_SOURCE" ]; then
    BINARY_SOURCE=$(find build -type f -name "NativeWebViewWrapper" 2>/dev/null | grep -v "dist/" | head -n 1)
fi

if [ -n "$BINARY_SOURCE" ]; then
    echo "Found compiled binary at: $BINARY_SOURCE"
    cp "$BINARY_SOURCE" dist/BOOK_TO_GAME.app/Contents/MacOS/BOOK_TO_GAME
    chmod +x dist/BOOK_TO_GAME.app/Contents/MacOS/BOOK_TO_GAME
else
    echo "ERROR: Compiled executable binary not found in build directory!"
    find build -type f || true
    exit 1
fi

# 3. Locate the compiled PuterBridge binary anywhere in the build tree and copy it in
BRIDGE_SOURCE=$(find build -type f -name "PuterBridge" 2>/dev/null | grep -v "dist/" | head -n 1)
if [ -n "$BRIDGE_SOURCE" ]; then
    echo "Found PuterBridge binary at: $BRIDGE_SOURCE"
    cp "$BRIDGE_SOURCE" dist/BOOK_TO_GAME.app/Contents/MacOS/PuterBridge
    chmod +x dist/BOOK_TO_GAME.app/Contents/MacOS/PuterBridge
else
    echo "WARNING: PuterBridge executable binary not found in build directory!"
fi

echo "=== 5. Zipping App Bundle ==="
cd dist && zip -r ../book-to-game-macos-universal.zip BOOK_TO_GAME.app

echo ""
echo "=================================================================="
echo "SUCCESS: book-to-game-macos-universal.zip is ready in root folder!"
echo "=================================================================="
echo ""
